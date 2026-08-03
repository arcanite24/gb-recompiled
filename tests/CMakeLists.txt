find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
    NAME accuracy_runner_rejects_empty_selection
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/run_tests.py"
        --filter __gbrecomp_no_matching_test__
)
set_tests_properties(accuracy_runner_rejects_empty_selection PROPERTIES
    WILL_FAIL TRUE
)

add_test(
    NAME accuracy_runner_exit_policy
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_accuracy_runner_policy.py"
)

add_test(
    NAME accuracy_runner_cache_provenance
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_accuracy_runner_cache.py"
)

add_test(
    NAME nl0_profile_summary_estimates_dynamic_coverage
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_nl0_profile_summary.py"
)

add_test(
    NAME nl0_profile_runner_requires_cycle_anchored_input
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_nl0_profile_runner.py"
)

add_test(
    NAME crystal_route_validator_accepts_matching_checkpoint
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_route_validator.py"
)

add_test(
    NAME crystal_determinism_validator_accepts_three_matching_runs
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_determinism_validator.py"
)

add_test(
    NAME crystal_challenge_replay_preflight_is_provenance_complete
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_challenge_replay_preflight.py"
)

add_test(
    NAME crystal_challenge_save_lifecycle_is_vanilla_compatible
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_challenge_lifecycle.py"
)

add_test(
    NAME multi_rom_analysis_isolation
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_multi_rom_analysis_isolation.py"
        --gbrecomp "$<TARGET_FILE:gbrecomp>"
        --fixture-generator
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_mapper_rom.py"
)

add_test(
    NAME release_bundle_is_self_contained
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_release_bundle.py"
        --gbrecomp "$<TARGET_FILE:gbrecomp>"
        --runtime-dir "${PROJECT_SOURCE_DIR}/runtime"
        --fixture-generator
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_mapper_rom.py"
)
set_tests_properties(release_bundle_is_self_contained PROPERTIES TIMEOUT 180)

add_test(
    NAME generation_progress_is_machine_readable_and_private
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_generation_progress.py"
        --gbrecomp "$<TARGET_FILE:gbrecomp>"
        --runtime-dir "${PROJECT_SOURCE_DIR}/runtime"
        --fixture-generator
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_mapper_rom.py"
)
set_tests_properties(
    generation_progress_is_machine_readable_and_private
    PROPERTIES TIMEOUT 180
)

add_test(
    NAME crystal_first_run_rejects_before_cache_creation
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_first_run_privacy.py"
        --first-run
        "${PROJECT_SOURCE_DIR}/ports/pokemon-crystal/scripts/first_run.py"
        --privacy-verifier
        "${PROJECT_SOURCE_DIR}/ports/pokemon-crystal/scripts/verify_first_run_privacy.py"
)

add_test(
    NAME crystal_release_package_is_deterministic_and_relocatable
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_release_package.py"
        --gbrecomp "$<TARGET_FILE:gbrecomp>"
        --runtime "${CMAKE_SOURCE_DIR}/runtime"
        --license "${CMAKE_SOURCE_DIR}/LICENSE"
        --readme "${CMAKE_SOURCE_DIR}/README.md"
        --distribution-tool
        "${CMAKE_SOURCE_DIR}/tools/create_gbrecomp_distribution.py"
        --eject-script
        "${CMAKE_SOURCE_DIR}/ports/pokemon-crystal/scripts/eject.py"
        --packager
        "${CMAKE_SOURCE_DIR}/ports/pokemon-crystal/scripts/create_release.py"
)
set_tests_properties(
    crystal_release_package_is_deterministic_and_relocatable
    PROPERTIES TIMEOUT 240
)

add_test(
    NAME crystal_packaged_verifier_diagnostics_are_path_redacted
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_packaged_verifier_diagnostics.py"
        --verifier
        "${CMAKE_SOURCE_DIR}/ports/pokemon-crystal/scripts/verify_packaged_release.py"
)

add_test(
    NAME crystal_controller_verifier_is_fail_closed
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_controller_verifier.py"
        --verifier
        "${CMAKE_SOURCE_DIR}/ports/pokemon-crystal/scripts/verify_controller_release.py"
)

add_test(
    NAME crystal_rom_secret_reconstruction_is_exact_and_fail_closed
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_rom_secret_reconstruction.py"
        --helper
        "${CMAKE_SOURCE_DIR}/tools/reconstruct_crystal_rom.py"
)

add_test(
    NAME crystal_standalone_contract_is_versioned_and_fail_closed
    COMMAND
        ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_standalone_contract.py"
        --gbrecomp "$<TARGET_FILE:gbrecomp>"
        --runtime "${CMAKE_SOURCE_DIR}/runtime"
        --license "${CMAKE_SOURCE_DIR}/LICENSE"
        --readme "${CMAKE_SOURCE_DIR}/README.md"
        --distribution-tool
        "${CMAKE_SOURCE_DIR}/tools/create_gbrecomp_distribution.py"
        --eject-script
        "${CMAKE_SOURCE_DIR}/ports/pokemon-crystal/scripts/eject.py"
)
set_tests_properties(
    crystal_standalone_contract_is_versioned_and_fail_closed
    PROPERTIES TIMEOUT 180
)

add_test(
    NAME native_patch_sdk_end_to_end
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_native_patch_e2e.py"
        --gbrecomp "$<TARGET_FILE:gbrecomp>"
        --fixture-generator
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_mapper_rom.py"
)
set_tests_properties(native_patch_sdk_end_to_end PROPERTIES TIMEOUT 360)

add_executable(test_mbc5_rom_bank_id
    test_mbc5_rom_bank_id.cpp
)
target_link_libraries(test_mbc5_rom_bank_id PRIVATE gbrecomp_core)
add_test(NAME mbc5_rom_bank_id COMMAND test_mbc5_rom_bank_id)

add_executable(test_mbc5_analysis test_mbc5_analysis.cpp)
target_link_libraries(test_mbc5_analysis PRIVATE gbrecomp_core)
add_test(NAME mbc5_analysis_uses_9_bit_banks COMMAND test_mbc5_analysis)
set_tests_properties(mbc5_analysis_uses_9_bit_banks PROPERTIES TIMEOUT 5)

add_executable(test_mbc5_codegen test_mbc5_codegen.cpp)
target_link_libraries(test_mbc5_codegen PRIVATE gbrecomp_core)
add_test(NAME mbc5_codegen_uses_9_bit_banks COMMAND test_mbc5_codegen)

add_executable(test_codegen_bus_phase test_codegen_bus_phase.cpp)
target_link_libraries(test_codegen_bus_phase PRIVATE gbrecomp_core)
add_test(NAME codegen_places_memory_access_in_final_mcycle COMMAND test_codegen_bus_phase)

add_executable(test_analyzer_call_clobber test_analyzer_call_clobber.cpp)
target_link_libraries(test_analyzer_call_clobber PRIVATE gbrecomp_core)
add_test(NAME analyzer_call_clobbers_constants COMMAND test_analyzer_call_clobber)

add_executable(test_analyzer_register_effects test_analyzer_register_effects.cpp)
target_link_libraries(test_analyzer_register_effects PRIVATE gbrecomp_core)
add_test(NAME analyzer_invalidates_written_registers COMMAND test_analyzer_register_effects)

add_executable(test_analyzer_state_join test_analyzer_state_join.cpp)
target_link_libraries(test_analyzer_state_join PRIVATE gbrecomp_core)
add_test(NAME analyzer_joins_control_flow_state COMMAND test_analyzer_state_join)

add_executable(test_analyzer_mapper_state test_analyzer_mapper_state.cpp)
target_link_libraries(test_analyzer_mapper_state PRIVATE gbrecomp_core)
add_test(NAME analyzer_models_mapper_registers COMMAND test_analyzer_mapper_state)

add_executable(test_rgbds_symbol_parser test_rgbds_symbol_parser.cpp)
target_link_libraries(test_rgbds_symbol_parser PRIVATE gbrecomp_core)
add_test(NAME rgbds_symbol_parser_handles_constants_and_aliases
         COMMAND test_rgbds_symbol_parser)

add_executable(test_analysis_diagnostics test_analysis_diagnostics.cpp)
target_link_libraries(test_analysis_diagnostics PRIVATE gbrecomp_core)
add_test(NAME analyzer_exports_actionable_diagnostics
         COMMAND test_analysis_diagnostics)

add_test(NAME crystal_semantic_anchor_schema_rejects_invalid_ranges
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_semantic_anchors.py)

add_test(NAME crystal_analysis_diagnostics_are_actionable
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_analysis_diagnostics.py)

add_test(NAME crystal_semantic_package_is_fail_closed
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_semantic_package.py)

add_test(NAME crystal_semantic_accessors_are_typed_and_bounded
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_semantic_accessors.py)

add_test(NAME crystal_native_pc_model_preserves_records
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_pc_model.py)

add_test(NAME crystal_port_module_is_fail_closed
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_port_module.py)
add_test(NAME crystal_port_extensions_are_ordered_and_fail_closed
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_port_extensions.py)

add_test(NAME crystal_workbench_evidence_is_deterministic_and_honest
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_workbench_validator.py)

add_test(NAME crystal_workbench_is_observational_across_runtime_modes
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_workbench_observational.py)

add_test(NAME crystal_native_pokedex_evidence_is_fail_closed
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_native_pokedex_verifier.py)
add_test(NAME crystal_native_bills_pc_evidence_is_fail_closed
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_native_bills_pc_verifier.py)
add_test(NAME data_mod_manifest_is_deterministic_and_fail_closed
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_data_mod_manifest.py)
add_test(NAME crystal_data_mod_compiler_uses_semantic_identities
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_data_mod_compiler.py)
add_test(NAME crystal_data_mod_samples_are_independent_and_deterministic
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_data_mod_samples.py)
add_test(NAME data_mod_replay_preflight_is_provenance_complete
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_data_mod_replay.py)

add_test(NAME crystal_transactional_persistence_is_fail_closed
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/test_crystal_transactional_persistence_verifier.py)

add_test(
    NAME generated_port_extension_end_to_end
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/test_port_extension_e2e.py"
        --gbrecomp "$<TARGET_FILE:gbrecomp>"
        --fixture-generator
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_mapper_rom.py"
)
set_tests_properties(generated_port_extension_end_to_end PROPERTIES TIMEOUT 180)

add_executable(test_mbc1_lower_window_analysis test_mbc1_lower_window_analysis.cpp)
target_link_libraries(test_mbc1_lower_window_analysis PRIVATE gbrecomp_core)
add_test(NAME analyzer_compiles_mbc1_lower_window COMMAND test_mbc1_lower_window_analysis)

set(GBRECOMP_TEST_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/mbc1_mode1_lower_window")
set(GBRECOMP_TEST_ROM
    "${CMAKE_CURRENT_BINARY_DIR}/fixtures/mbc1_mode1_lower_window.gb")
set(GBRECOMP_TEST_INTERNAL_HEADER
    "${GBRECOMP_TEST_GENERATED_DIR}/mbc1_mode1_lower_window_internal.h")

add_custom_command(
    OUTPUT
        "${GBRECOMP_TEST_INTERNAL_HEADER}"
        "${GBRECOMP_TEST_GENERATED_DIR}/mbc1_mode1_lower_window.h"
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_mapper_rom.py"
        --mapper mbc1
        --output "${GBRECOMP_TEST_ROM}"
    COMMAND
        "$<TARGET_FILE:gbrecomp>"
        "${GBRECOMP_TEST_ROM}"
        --no-scan
        -o "${GBRECOMP_TEST_GENERATED_DIR}"
    DEPENDS
        gbrecomp
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_mapper_rom.py"
    COMMENT "Generating the synthetic MBC1 test project"
    VERBATIM
)

if(TARGET gbrt)
    add_executable(test_mbc5_runtime_bank test_mbc5_runtime_bank.c)
    target_link_libraries(test_mbc5_runtime_bank PRIVATE gbrt)
    add_test(NAME mbc5_runtime_uses_9_bit_banks COMMAND test_mbc5_runtime_bank)

    add_executable(test_runtime_rom_mapping test_runtime_rom_mapping.c)
    target_link_libraries(test_runtime_rom_mapping PRIVATE gbrt)
    add_test(NAME runtime_uses_one_rom_mapping COMMAND test_runtime_rom_mapping)

    add_executable(test_hdma_timing test_hdma_timing.c)
    target_link_libraries(test_hdma_timing PRIVATE gbrt)
    add_test(NAME hdma_charges_hardware_cycles COMMAND test_hdma_timing)

    add_executable(test_oam_dma_bus test_oam_dma_bus.c)
    target_link_libraries(test_oam_dma_bus PRIVATE gbrt)
    add_test(NAME oam_dma_uses_model_specific_buses COMMAND test_oam_dma_bus)

    add_executable(test_oam_corruption test_oam_corruption.c)
    target_link_libraries(test_oam_corruption PRIVATE gbrt)
    add_test(NAME dmg_inc16_corrupts_current_oam_scan_row COMMAND test_oam_corruption)

    add_executable(test_timer_reload test_timer_reload.c)
    target_link_libraries(test_timer_reload PRIVATE gbrt)
    add_test(NAME timer_preserves_overflow_reload_edges COMMAND test_timer_reload)

    add_executable(test_timer_arithmetic test_timer_arithmetic.c)
    target_link_libraries(test_timer_arithmetic PRIVATE gbrt)
    add_test(NAME arithmetic_timer_matches_scalar_oracle COMMAND test_timer_arithmetic)

    add_executable(test_interrupt_entry test_interrupt_entry.c)
    target_link_libraries(test_interrupt_entry PRIVATE gbrt)
    add_test(NAME interrupt_entry_rechecks_ie_if_during_push COMMAND test_interrupt_entry)

    add_executable(test_halt_bug test_halt_bug.c)
    target_link_libraries(test_halt_bug PRIVATE gbrt)
    add_test(NAME halt_bug_suppresses_one_opcode_fetch_increment COMMAND test_halt_bug)

    add_executable(test_audio_chunk_invariance test_audio_chunk_invariance.c)
    target_link_libraries(test_audio_chunk_invariance PRIVATE gbrt)
    add_test(NAME audio_pcm_is_invariant_to_scheduler_batch_size COMMAND test_audio_chunk_invariance)

    if(SDL2_FOUND)
        add_executable(test_audio_callback_concurrency test_audio_callback_concurrency.cpp)
        target_link_libraries(test_audio_callback_concurrency PRIVATE gbrt)
        add_test(NAME audio_callback_uses_race_free_batched_ring COMMAND test_audio_callback_concurrency)

        add_executable(test_input_script test_input_script.cpp)
        target_link_libraries(test_input_script PRIVATE gbrt)
        add_test(NAME input_script_rejects_malformed_tokens COMMAND test_input_script)

        add_executable(test_persistence_directory test_persistence_directory.cpp)
        target_link_libraries(test_persistence_directory PRIVATE gbrt)
        add_test(NAME persistence_directory_isolates_battery_save COMMAND test_persistence_directory)

        add_executable(test_screenshot_prefix test_screenshot_prefix.cpp)
        target_link_libraries(test_screenshot_prefix PRIVATE gbrt)
        add_test(NAME screenshot_prefix_preserves_long_evidence_path COMMAND test_screenshot_prefix)
    endif()

    add_executable(test_ppu_dot_timing test_ppu_dot_timing.c)
    target_link_libraries(test_ppu_dot_timing PRIVATE gbrt)
    add_test(NAME ppu_uses_dot_aware_pixel_transfer COMMAND test_ppu_dot_timing)

    add_executable(test_runtime_bus_phase test_runtime_bus_phase.c)
    target_link_libraries(test_runtime_bus_phase PRIVATE gbrt)
    add_test(NAME ram_stub_places_memory_access_in_final_mcycle COMMAND test_runtime_bus_phase)

    add_executable(test_state_json test_state_json.c)
    target_link_libraries(test_state_json PRIVATE gbrt)
    add_test(NAME state_json_includes_full_hram COMMAND test_state_json)

    add_executable(test_runtime_test_breakpoint test_runtime_test_breakpoint.c)
    target_link_libraries(test_runtime_test_breakpoint PRIVATE gbrt)
    add_test(NAME runtime_stops_on_opt_in_test_breakpoint COMMAND test_runtime_test_breakpoint)

    add_executable(test_interpreter_diagnostics test_interpreter_diagnostics.c)
    target_link_libraries(test_interpreter_diagnostics PRIVATE gbrt)
    add_test(NAME interpreter_diagnostics_record_control_flow_exit COMMAND test_interpreter_diagnostics)

    add_executable(test_performance_counters test_performance_counters.c)
    target_link_libraries(test_performance_counters PRIVATE gbrt)
    add_test(NAME performance_counters_preserve_stop_semantics COMMAND test_performance_counters)

    add_executable(test_event_deadline test_event_deadline.c)
    target_link_libraries(test_event_deadline PRIVATE gbrt)
    add_test(NAME runtime_reports_conservative_next_event_deadline COMMAND test_event_deadline)

    add_executable(test_semantic_reader test_semantic_reader.c)
    target_link_libraries(test_semantic_reader PRIVATE gbrt)
    add_test(NAME semantic_reader_is_bounded_and_exact_rom
             COMMAND test_semantic_reader)

    add_executable(test_gameplay_mutation test_gameplay_mutation.c)
    target_link_libraries(test_gameplay_mutation PRIVATE gbrt)
    add_test(NAME gameplay_mutation_is_atomic_and_fail_closed
             COMMAND test_gameplay_mutation)

    add_executable(test_host_configuration test_host_configuration.c)
    target_link_libraries(test_host_configuration PRIVATE gbrt)
    add_test(NAME host_configuration_is_canonical_and_fail_closed
             COMMAND test_host_configuration)

    add_executable(test_crystal_challenge_patch
        test_crystal_challenge_patch.c
        "${PROJECT_SOURCE_DIR}/ports/pokemon-crystal/native-patches/challenge-mode/challenge_mode_patch.c"
        "${PROJECT_SOURCE_DIR}/ports/pokemon-crystal/native-patches/challenge-mode/challenge_rules.c"
    )
    target_link_libraries(test_crystal_challenge_patch PRIVATE gbrt)
    add_test(NAME crystal_challenge_trainer_guard_is_fail_closed
             COMMAND test_crystal_challenge_patch)

    add_executable(test_crystal_challenge_rules
        test_crystal_challenge_rules.c
        "${PROJECT_SOURCE_DIR}/ports/pokemon-crystal/native-patches/challenge-mode/challenge_rules.c"
    )
    target_include_directories(test_crystal_challenge_rules PRIVATE
        "${PROJECT_SOURCE_DIR}/ports/pokemon-crystal/native-patches/challenge-mode"
    )
    target_link_libraries(test_crystal_challenge_rules PRIVATE gbrt)
    add_test(NAME crystal_challenge_rules_are_bounded_and_deterministic
             COMMAND test_crystal_challenge_rules)

    add_executable(test_data_mod_overlay test_data_mod_overlay.c)
    target_link_libraries(test_data_mod_overlay PRIVATE gbrt)
    add_test(NAME data_mod_overlay_preserves_original_rom
             COMMAND test_data_mod_overlay)

    add_executable(test_port_extension test_port_extension.c)
    target_link_libraries(test_port_extension PRIVATE gbrt)
    add_test(NAME port_extension_is_versioned_headless_and_exact_rom
             COMMAND test_port_extension)

    add_executable(test_presentation_contract test_presentation_contract.c)
    target_link_libraries(test_presentation_contract PRIVATE gbrt)
    add_test(NAME native_presentation_is_exact_rom_and_gpu_independent
             COMMAND test_presentation_contract)

    add_executable(test_widescreen_compositor test_widescreen_compositor.c)
    target_link_libraries(test_widescreen_compositor PRIVATE gbrt)
    add_test(NAME widescreen_compositor_is_bounded_and_renderer_independent
             COMMAND test_widescreen_compositor)

    add_executable(test_presentation_replacements
        test_presentation_replacements.c)
    target_link_libraries(test_presentation_replacements PRIVATE gbrt)
    add_test(NAME presentation_replacements_are_hashed_and_provenanced
             COMMAND test_presentation_replacements)

    add_executable(test_generated_mbc1_fast_read
        test_generated_mbc1_fast_read.c
        "${GBRECOMP_TEST_INTERNAL_HEADER}"
    )
    target_include_directories(test_generated_mbc1_fast_read PRIVATE
        "${GBRECOMP_TEST_GENERATED_DIR}"
    )
    target_link_libraries(test_generated_mbc1_fast_read PRIVATE gbrt)

    add_test(
        NAME generated_mbc1_mode1_lower_window
        COMMAND test_generated_mbc1_fast_read
    )
else()
    message(WARNING "Skipping generated runtime tests because gbrt is unavailable")
endif()
