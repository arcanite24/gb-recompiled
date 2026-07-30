#include "recompiler/codegen/c_emitter.h"
#include "recompiler/ir/ir.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

int main() {
    gbrecomp::ir::Program program;
    program.rom_name = "bus-phase";

    gbrecomp::ir::IRInstruction read_stat;
    read_stat.opcode = gbrecomp::ir::Opcode::IO_READ;
    read_stat.dst = gbrecomp::ir::Operand::reg8(7);
    read_stat.src = gbrecomp::ir::Operand::imm8(0x41);
    read_stat.source_bank = 0;
    read_stat.source_address = 0x0100;
    read_stat.cycles = 12;

    gbrecomp::ir::BasicBlock block;
    block.id = 0;
    block.label = "block_0100";
    block.bank = 0;
    block.start_address = 0x0100;
    block.end_address = 0x0103;
    block.is_entry = true;
    block.instructions.push_back(read_stat);

    gbrecomp::ir::IRInstruction test_breakpoint;
    test_breakpoint.opcode = gbrecomp::ir::Opcode::MOV_REG_REG;
    test_breakpoint.dst = gbrecomp::ir::Operand::reg8(0);
    test_breakpoint.src = gbrecomp::ir::Operand::reg8(0);
    test_breakpoint.source_bank = 0;
    test_breakpoint.source_address = 0x0102;
    test_breakpoint.cycles = 4;
    block.instructions.push_back(test_breakpoint);

    gbrecomp::ir::IRInstruction bit_hl;
    bit_hl.opcode = gbrecomp::ir::Opcode::BIT;
    bit_hl.dst = gbrecomp::ir::Operand::reg8(6);
    bit_hl.src = gbrecomp::ir::Operand::bit_idx(0);
    bit_hl.source_bank = 0;
    bit_hl.source_address = 0x0103;
    bit_hl.has_source_location = true;
    bit_hl.cycles = 12;
    block.instructions.push_back(bit_hl);

    gbrecomp::ir::IRInstruction inc_hl;
    inc_hl.opcode = gbrecomp::ir::Opcode::INC8;
    inc_hl.dst = gbrecomp::ir::Operand::reg8(6);
    inc_hl.source_bank = 0;
    inc_hl.source_address = 0x0105;
    inc_hl.has_source_location = true;
    inc_hl.cycles = 12;
    block.instructions.push_back(inc_hl);

    gbrecomp::ir::IRInstruction rlc_hl;
    rlc_hl.opcode = gbrecomp::ir::Opcode::RLC;
    rlc_hl.dst = gbrecomp::ir::Operand::reg8(6);
    rlc_hl.source_bank = 0;
    rlc_hl.source_address = 0x0106;
    rlc_hl.has_source_location = true;
    rlc_hl.cycles = 16;
    block.instructions.push_back(rlc_hl);

    gbrecomp::ir::IRInstruction push;
    push.opcode = gbrecomp::ir::Opcode::PUSH16;
    push.dst = gbrecomp::ir::Operand::reg16(0);
    push.source_bank = 0;
    push.source_address = 0x0103;
    push.has_source_location = true;
    push.cycles = 16;
    block.instructions.push_back(push);

    gbrecomp::ir::IRInstruction pop;
    pop.opcode = gbrecomp::ir::Opcode::POP16;
    pop.dst = gbrecomp::ir::Operand::reg16(0);
    pop.source_bank = 0;
    pop.source_address = 0x0104;
    pop.has_source_location = true;
    pop.cycles = 12;
    block.instructions.push_back(pop);

    gbrecomp::ir::IRInstruction add_sp;
    add_sp.opcode = gbrecomp::ir::Opcode::ADD_SP_IMM8;
    add_sp.src = gbrecomp::ir::Operand::offset(0x42);
    add_sp.source_bank = 0;
    add_sp.source_address = 0x0105;
    add_sp.has_source_location = true;
    add_sp.cycles = 16;
    block.instructions.push_back(add_sp);

    gbrecomp::ir::IRInstruction ld_hl_sp;
    ld_hl_sp.opcode = gbrecomp::ir::Opcode::LD_HL_SP_N;
    ld_hl_sp.src = gbrecomp::ir::Operand::offset(0x42);
    ld_hl_sp.source_bank = 0;
    ld_hl_sp.source_address = 0x0107;
    ld_hl_sp.has_source_location = true;
    ld_hl_sp.cycles = 12;
    block.instructions.push_back(ld_hl_sp);

    gbrecomp::ir::IRInstruction ldi_read;
    ldi_read.opcode = gbrecomp::ir::Opcode::LOAD8_HL_AUTO;
    ldi_read.dst = gbrecomp::ir::Operand::reg8(7);
    ldi_read.src = gbrecomp::ir::Operand::offset(1);
    ldi_read.source_bank = 0;
    ldi_read.source_address = 0x010A;
    ldi_read.has_source_location = true;
    ldi_read.cycles = 8;
    block.instructions.push_back(ldi_read);

    gbrecomp::ir::IRInstruction ldd_read = ldi_read;
    ldd_read.src = gbrecomp::ir::Operand::offset(-1);
    ldd_read.source_address = 0x010B;
    block.instructions.push_back(ldd_read);

    gbrecomp::ir::IRInstruction ldi_write;
    ldi_write.opcode = gbrecomp::ir::Opcode::STORE8_HL_AUTO;
    ldi_write.dst = gbrecomp::ir::Operand::offset(1);
    ldi_write.src = gbrecomp::ir::Operand::reg8(7);
    ldi_write.source_bank = 0;
    ldi_write.source_address = 0x010C;
    ldi_write.has_source_location = true;
    ldi_write.cycles = 8;
    block.instructions.push_back(ldi_write);

    gbrecomp::ir::IRInstruction ldd_write = ldi_write;
    ldd_write.dst = gbrecomp::ir::Operand::offset(-1);
    ldd_write.source_address = 0x010D;
    block.instructions.push_back(ldd_write);

    const std::vector<std::pair<gbrecomp::ir::Opcode, const char*>> alu_hl_ops = {
        {gbrecomp::ir::Opcode::ADD8, "gb_add8"},
        {gbrecomp::ir::Opcode::ADC8, "gb_adc8"},
        {gbrecomp::ir::Opcode::SUB8, "gb_sub8"},
        {gbrecomp::ir::Opcode::SBC8, "gb_sbc8"},
        {gbrecomp::ir::Opcode::AND8, "gb_and8"},
        {gbrecomp::ir::Opcode::OR8, "gb_or8"},
        {gbrecomp::ir::Opcode::XOR8, "gb_xor8"},
        {gbrecomp::ir::Opcode::CP8, "gb_cp8"},
    };
    uint16_t alu_address = 0x0110;
    for (const auto& [opcode, helper] : alu_hl_ops) {
        (void)helper;
        gbrecomp::ir::IRInstruction alu_hl;
        alu_hl.opcode = opcode;
        alu_hl.dst = gbrecomp::ir::Operand::reg8(7);
        alu_hl.src = gbrecomp::ir::Operand::reg8(6);
        alu_hl.source_bank = 0;
        alu_hl.source_address = alu_address++;
        alu_hl.has_source_location = true;
        alu_hl.cycles = 8;
        block.instructions.push_back(alu_hl);
    }
    block.instructions.push_back(
        gbrecomp::ir::IRInstruction::make_ret(0, 0x0109)
    );
    program.blocks.emplace(block.id, block);

    gbrecomp::ir::Function function;
    function.name = "gb_main";
    function.bank = 0;
    function.entry_address = 0x0100;
    function.block_ids.push_back(block.id);
    function.is_entry_point = true;
    program.functions.emplace(function.name, function);

    gbrecomp::ir::BasicBlock control_block;
    control_block.id = 1;
    control_block.label = "block_0200";
    control_block.bank = 0;
    control_block.start_address = 0x0200;
    control_block.end_address = 0x0209;
    control_block.is_entry = true;

    gbrecomp::ir::IRInstruction call;
    call.opcode = gbrecomp::ir::Opcode::CALL;
    call.dst = gbrecomp::ir::Operand::imm16(0x0300);
    call.dst.bank = gbrecomp::UNKNOWN_BANK;
    call.source_bank = 0;
    call.source_address = 0x0200;
    call.has_source_location = true;
    call.cycles = 24;
    control_block.instructions.push_back(call);

    gbrecomp::ir::IRInstruction jump;
    jump.opcode = gbrecomp::ir::Opcode::JUMP;
    jump.dst = gbrecomp::ir::Operand::imm16(0x0310);
    jump.dst.bank = gbrecomp::UNKNOWN_BANK;
    jump.source_bank = 0;
    jump.source_address = 0x0203;
    jump.has_source_location = true;
    jump.cycles = 16;
    control_block.instructions.push_back(jump);

    gbrecomp::ir::IRInstruction rst;
    rst.opcode = gbrecomp::ir::Opcode::RST;
    rst.dst = gbrecomp::ir::Operand::rst_vec(0x38);
    rst.source_bank = 0;
    rst.source_address = 0x0206;
    rst.has_source_location = true;
    rst.cycles = 16;
    control_block.instructions.push_back(rst);

    gbrecomp::ir::IRInstruction reti;
    reti.opcode = gbrecomp::ir::Opcode::RETI;
    reti.source_bank = 0;
    reti.source_address = 0x0207;
    reti.has_source_location = true;
    reti.cycles = 16;
    control_block.instructions.push_back(reti);
    program.blocks.emplace(control_block.id, control_block);

    gbrecomp::ir::Function control_function;
    control_function.name = "control_test";
    control_function.bank = 0;
    control_function.entry_address = 0x0200;
    control_function.block_ids.push_back(control_block.id);
    control_function.is_entry_point = true;
    program.functions.emplace(control_function.name, control_function);

    gbrecomp::ir::BasicBlock halt_block;
    halt_block.id = 2;
    halt_block.label = "block_0400";
    halt_block.bank = 0;
    halt_block.start_address = 0x0400;
    halt_block.end_address = 0x0401;
    halt_block.is_entry = true;

    gbrecomp::ir::IRInstruction halt;
    halt.opcode = gbrecomp::ir::Opcode::HALT;
    halt.source_bank = 0;
    halt.source_address = 0x0400;
    halt.has_source_location = true;
    halt.cycles = 4;
    halt_block.instructions.push_back(halt);
    program.blocks.emplace(halt_block.id, halt_block);

    gbrecomp::ir::Function halt_function;
    halt_function.name = "halt_test";
    halt_function.bank = 0;
    halt_function.entry_address = 0x0400;
    halt_function.block_ids.push_back(halt_block.id);
    halt_function.is_entry_point = true;
    program.functions.emplace(halt_function.name, halt_function);

    std::vector<uint8_t> rom(0x150, 0);
    gbrecomp::codegen::GeneratorOptions options;
    options.output_prefix = "bus_phase";
    const auto output = gbrecomp::codegen::generate_output(
        program, rom.data(), rom.size(), options
    );

    std::string generated = output.source_content;
    for (const auto& extra : output.extra_files) {
        generated += extra.content;
    }

    const std::string expected =
        "gb_tick(ctx, 11);\n"
        "    ctx->a = gb_read8(ctx, GB_IO_STAT);\n"
        "    gb_tick(ctx, 1);";
    if (generated.find(expected) == std::string::npos) {
        std::cerr << "generated I/O read was not placed in the final M-cycle\n";
        return 1;
    }
    if (generated.find("gb_tick(ctx, 12);\n"
                       "    ctx->a = gb_read8(ctx, GB_IO_STAT);") !=
        std::string::npos) {
        std::cerr << "generated I/O read still occurs after all instruction cycles\n";
        return 1;
    }
    const std::string expected_bit_hl =
        "gb_bit(ctx, 0, "
        "gbrt_timed_bus_read8(ctx, ctx->hl, 11));";
    if (generated.find(expected_bit_hl) == std::string::npos) {
        std::cerr << "generated BIT (HL) was not read in the final M-cycle\n";
        return 1;
    }
    if (generated.find(
            "gbrt_timed_bus_read8(ctx, gbrt_rmw_addr, 7);") ==
            std::string::npos ||
        generated.find(
            "gbrt_rmw_value = gb_inc8(ctx, gbrt_rmw_value);") ==
            std::string::npos ||
        generated.find(
            "gbrt_timed_bus_rmw_write8(ctx, gbrt_rmw_addr, "
            "gbrt_rmw_value);") ==
            std::string::npos) {
        std::cerr << "generated INC (HL) did not begin its final write M-cycle\n";
        return 1;
    }
    if (generated.find(
            "gbrt_timed_bus_read8(ctx, gbrt_rmw_addr, 11);") ==
            std::string::npos ||
        generated.find(
            "gbrt_rmw_value = gb_rlc(ctx, gbrt_rmw_value);") ==
            std::string::npos) {
        std::cerr << "generated CB (HL) did not split read and write M-cycles\n";
        return 1;
    }
    for (const auto& [opcode, helper] : alu_hl_ops) {
        (void)opcode;
        const std::string expected_alu_hl =
            "gb_tick(ctx, 7);\n"
            "    " + std::string(helper) +
            "(ctx, gb_read8(ctx, ctx->hl));\n"
            "    gb_tick(ctx, 1);";
        if (generated.find(expected_alu_hl) == std::string::npos) {
            std::cerr << "generated " << helper
                      << " (HL) read was not placed in the final M-cycle\n";
            return 1;
        }
    }
    if (generated.find("if (gbrt_test_breakpoint_enabled)") == std::string::npos) {
        std::cerr << "generated LD B,B does not honor opt-in test breakpoints\n";
        return 1;
    }
    if (generated.find("gbrt_execute_halt(ctx, 0x401, 4);") ==
        std::string::npos) {
        std::cerr << "generated HALT bypasses the shared HALT-bug contract\n";
        return 1;
    }
    if (generated.find("gbrt_timed_push16(ctx, ctx->bc);") == std::string::npos ||
        generated.find("ctx->bc = gbrt_timed_pop16(ctx);") == std::string::npos ||
        generated.find("gbrt_timed_call(ctx, 0x300, 0x203);") == std::string::npos ||
        generated.find("gbrt_timed_jump(ctx, 0x0310, 16);") == std::string::npos ||
        generated.find("gbrt_timed_rst(ctx, 0x38, 0x207);") == std::string::npos ||
        generated.find("gbrt_timed_add_sp(ctx, 0x0106);") == std::string::npos ||
        generated.find("gbrt_timed_ld_hl_sp_n(ctx, 0x0108);") == std::string::npos ||
        generated.find("gbrt_timed_ret(ctx);") == std::string::npos ||
        generated.find("gbrt_timed_reti(ctx);") == std::string::npos) {
        std::cerr << "generated stack/control instructions bypass timed bus primitives\n";
        return 1;
    }
    if (generated.find("ctx->a = gbrt_timed_hl_read_auto(ctx, 1);") ==
            std::string::npos ||
        generated.find("ctx->a = gbrt_timed_hl_read_auto(ctx, -1);") ==
            std::string::npos ||
        generated.find("gbrt_timed_hl_write_auto(ctx, ctx->a, 1);") ==
            std::string::npos ||
        generated.find("gbrt_timed_hl_write_auto(ctx, ctx->a, -1);") ==
            std::string::npos) {
        std::cerr << "generated HL auto-index loads/stores bypass timed bus primitives\n";
        return 1;
    }
    if (output.main_content.find("--serial-stdout") == std::string::npos ||
        output.main_content.find("--stop-on-serial-verdict") ==
            std::string::npos ||
        output.main_content.find(
            "ctx->callbacks.on_serial_byte = gbrt_serial_stdout;") ==
            std::string::npos ||
        output.main_content.find("fflush(stdout);") == std::string::npos ||
        output.main_content.find("gbrt_serial_verdict_seen") ==
            std::string::npos) {
        std::cerr << "generated CLI does not expose stoppable opt-in serial output\n";
        return 1;
    }
    if (generated.find("gbrt_note_generated_direct_transition(ctx);") ==
            std::string::npos ||
        generated.find("gbrt_note_generated_indirect_dispatch(ctx);") ==
            std::string::npos ||
        generated.find("if (gbrt_generated_safepoint(ctx)) return;") ==
            std::string::npos) {
        std::cerr << "generated execution does not expose transition/safepoint counters\n";
        return 1;
    }
    if (output.main_content.find("--report-performance-counters") ==
            std::string::npos ||
        output.main_content.find("--estimate-visibility-regions") ==
            std::string::npos ||
        output.main_content.find("--scalar-timer") ==
            std::string::npos ||
        output.main_content.find("--eager-audio") ==
            std::string::npos ||
        output.main_content.find(
            "gbrt_visibility_estimator_enabled = estimate_visibility_regions;") ==
            std::string::npos ||
        output.main_content.find(
            "gbrt_force_scalar_timer = force_scalar_timer;") ==
            std::string::npos ||
        output.main_content.find(
            "gbrt_force_eager_audio = force_eager_audio;") ==
            std::string::npos ||
        output.main_content.find("gbrt_report_performance_counters(ctx);") ==
            std::string::npos ||
        output.cmake_content.find("GBRECOMP_ENABLE_PERFORMANCE_COUNTERS") ==
            std::string::npos ||
        output.cmake_content.find("GBRECOMP_GENERATED_COMPILE_JOBS") ==
            std::string::npos ||
        output.cmake_content.find("JOB_POOL_COMPILE") ==
            std::string::npos ||
        output.cmake_content.find("GBRT_ENABLE_PERFORMANCE_COUNTERS") ==
            std::string::npos) {
        std::cerr << "generated project does not expose compile-time-gated counter reporting\n";
        return 1;
    }
    if (generated.find("gbrt_note_generated_specialized_read(ctx)") ==
            std::string::npos ||
        generated.find("gbrt_note_generated_generic_read(ctx)") ==
            std::string::npos ||
        generated.find("gbrt_note_generated_specialized_write(ctx)") ==
            std::string::npos ||
        generated.find("gbrt_note_generated_generic_write(ctx)") ==
            std::string::npos) {
        std::cerr << "generated fast-memory paths do not expose attribution counters\n";
        return 1;
    }
    if (generated.find("#ifndef GBRT_DISABLE_GENERATED_FAST_MEMORY") ==
            std::string::npos) {
        std::cerr << "generated fast-memory paths do not expose an internal A/B control\n";
        return 1;
    }
    return 0;
}
