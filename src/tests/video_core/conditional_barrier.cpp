// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <deque>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <spirv/unified1/spirv.hpp11>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/environment.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace {
namespace IR = Shader::IR;
using NodeType = IR::AbstractSyntaxNode::Type;

class TestEnvironment final : public Shader::Environment {
public:
    TestEnvironment() { stage = Shader::Stage::Compute; }
    u64 ReadInstruction(u32) override { return 0; }
    u32 ReadCbufValue(u32, u32) override { return 0; }
    Shader::TextureType ReadTextureType(u32) override { return Shader::TextureType::Color2D; }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override { return false; }
    u32 ReadViewportTransformState() override { return 1; }
    u32 TextureBoundBuffer() const override { return 0; }
    u32 LocalMemorySize() const override { return 0; }
    u32 SharedMemorySize() const override { return 0; }
    std::array<u32, 3> WorkgroupSize() const override { return {64, 1, 1}; }
    bool HasHLEMacroState() const override { return false; }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}
};

enum class Guard { Uniform, Invocation };

// if (guard) { i = 0; do { BAR.SYNC; if (invocation_break && tid == i) break; } while (++i < n); }
// with n = c[3][4]. Mario Kart World's loading compute shaders 5f7463314fdbfc35 and
// 674f08299f626801 guard their loop with the invocation id.
struct BarrierLoop {
    Shader::ObjectPool<IR::Inst> pool{64};
    std::deque<IR::Block> blocks;
    IR::Program program;

    BarrierLoop(Guard guard, bool invocation_break) {
        IR::Block& entry{Block()};
        IR::Block& header{Block()};
        IR::Block& body{Block()};
        IR::Block& skip{Block()};
        IR::Block& next{Block()};
        IR::Block& loop_merge{Block()};
        IR::Block& merge{Block()};
        entry.AddBranch(&header);
        entry.AddBranch(&merge);
        header.AddBranch(&body);
        body.AddBranch(&loop_merge);
        body.AddBranch(&skip);
        skip.AddBranch(&next);
        next.AddBranch(&header);
        next.AddBranch(&loop_merge);
        loop_merge.AddBranch(&merge);

        IR::IREmitter start{entry};
        const IR::U32 tid{start.LocalInvocationIdX()};
        const IR::U32 count{start.GetCbuf(start.Imm32(3), start.Imm32(4))};
        const IR::U1 guard_cond{start.ConditionRef(
            start.INotEqual(guard == Guard::Invocation ? tid : count, start.Imm32(0)))};

        header.AppendNewInst(IR::Opcode::Phi, {});
        IR::Inst* const index{&header.back()};
        index->SetFlags(IR::Type::U32);

        IR::IREmitter loop{body};
        loop.Barrier();
        const IR::U1 break_cond{loop.ConditionRef(
            invocation_break ? loop.IEqual(tid, IR::U32{IR::Value{index}}) : loop.Imm1(false))};

        IR::IREmitter step{next};
        const IR::U32 incremented{step.IAdd(IR::U32{IR::Value{index}}, step.Imm32(1))};
        const IR::U1 repeat_cond{step.ConditionRef(step.ILessThan(incremented, count, false))};
        index->AddPhiOperand(&entry, start.Imm32(0));
        index->AddPhiOperand(&next, incremented);

        program.stage = Shader::Stage::Compute;
        program.workgroup_size = {64, 1, 1};
        program.post_order_blocks = {&merge, &loop_merge, &next, &skip, &body, &header, &entry};
        for (IR::Block& block : blocks) {
            program.blocks.push_back(&block);
        }
        program.syntax_list = {
            {.data{.block = &entry}, .type = NodeType::Block},
            {.data{.if_node{guard_cond, &header, &merge}}, .type = NodeType::If},
            {.data{.block = &header}, .type = NodeType::Block},
            {.data{.loop{&body, &next, &loop_merge}}, .type = NodeType::Loop},
            {.data{.block = &body}, .type = NodeType::Block},
            {.data{.break_node{break_cond, &loop_merge, &skip}}, .type = NodeType::Break},
            {.data{.block = &skip}, .type = NodeType::Block},
            {.data{.block = &next}, .type = NodeType::Block},
            {.data{.repeat{repeat_cond, &header, &loop_merge}}, .type = NodeType::Repeat},
            {.data{.block = &loop_merge}, .type = NodeType::Block},
            {.data{.end_if{&merge}}, .type = NodeType::EndIf},
            {.data{.block = &merge}, .type = NodeType::Block},
            {.type = NodeType::Return},
        };
        Shader::Optimization::ConditionalBarrierPass(program);
        TestEnvironment env;
        Shader::Optimization::CollectShaderInfoPass(env, program);
        program.info.constant_buffer_descriptors = {{3, 1}};
    }

    IR::Block& Block() {
        IR::Block& block{blocks.emplace_back(pool)};
        block.SetOrder(static_cast<u32>(blocks.size() - 1));
        return block;
    }

    size_t Count(IR::Opcode opcode) {
        size_t count{};
        for (IR::Block& block : blocks) {
            count += std::ranges::count(block.Instructions(), opcode, &IR::Inst::GetOpcode);
        }
        return count;
    }

    size_t CountSpirv(spv::Op opcode) {
        Shader::Profile profile{};
        profile.supported_spirv = 0x00010300;
        const std::vector<u32> code{Shader::Backend::SPIRV::EmitSPIRV(profile, program)};
        size_t count{};
        for (size_t offset = 5; offset < code.size(); offset += code[offset] >> 16) {
            REQUIRE((code[offset] >> 16) != 0);
            count += (code[offset] & 0xffffU) == static_cast<u32>(opcode);
        }
        return count;
    }
};
} // namespace

TEST_CASE("Barriers in loops only some invocations enter keep only their memory ordering",
          "[shader][barrier]") {
    BarrierLoop shader{Guard::Invocation, false};
    CHECK(shader.Count(IR::Opcode::Barrier) == 0);
    CHECK(shader.Count(IR::Opcode::WorkgroupMemoryBarrier) == 1);
    CHECK(shader.program.info.demoted_conditional_barrier);
    CHECK(shader.CountSpirv(spv::Op::OpControlBarrier) == 0);
    CHECK(shader.CountSpirv(spv::Op::OpMemoryBarrier) == 1);
}

TEST_CASE("Barriers in loops invocations leave on different iterations keep only their memory "
          "ordering",
          "[shader][barrier]") {
    BarrierLoop shader{Guard::Uniform, true};
    CHECK(shader.Count(IR::Opcode::Barrier) == 0);
    CHECK(shader.Count(IR::Opcode::WorkgroupMemoryBarrier) == 1);
    CHECK(shader.program.info.demoted_conditional_barrier);
}

TEST_CASE("Barriers under control flow the whole workgroup takes stay execution barriers",
          "[shader][barrier]") {
    BarrierLoop shader{Guard::Uniform, false};
    CHECK(shader.Count(IR::Opcode::Barrier) == 1);
    CHECK(shader.Count(IR::Opcode::WorkgroupMemoryBarrier) == 0);
    CHECK_FALSE(shader.program.info.demoted_conditional_barrier);
    CHECK(shader.CountSpirv(spv::Op::OpControlBarrier) == 1);
    CHECK(shader.CountSpirv(spv::Op::OpMemoryBarrier) == 0);
}
