/**
 * @file gbrt.h
 * @brief GameBoy Runtime Library
 * 
 * This runtime library provides the execution environment for recompiled
 * GameBoy games. It implements memory access, CPU context, and hardware
 * emulation needed by the generated C code.
 */

#ifndef GBRT_H
#define GBRT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "gbrt_host_configuration.h"

#ifdef __cplusplus
extern "C" {
#endif
/* ============================================================================
 * Configuration
 * ========================================================================== */

/**
 * @brief GameBoy model selection
 */
typedef enum {
    GB_MODEL_DMG,   /**< Original GameBoy (DMG) */
    GB_MODEL_CGB,   /**< GameBoy Color (CGB) */
    GB_MODEL_SGB,   /**< Super GameBoy */
} GBModel;

/**
 * @brief Runtime configuration
 */
typedef struct {
    GBModel model;
    bool cgb_compatibility_mode; /**< Run CGB hardware in DMG compatibility mode */
    bool cartridge_supports_cgb; /**< Cartridge advertises CGB support */
    bool cartridge_requires_cgb; /**< Cartridge is CGB-only */
    bool enable_bootrom;
    bool enable_audio;
    bool enable_serial;
    uint32_t speed_percent; /**< 100 = normal, 200 = 2x, etc */
    bool rtc_unix_time_override_enabled; /**< Use a deterministic wall-clock value for RTC persistence */
    uint64_t rtc_unix_time_override; /**< Unix seconds used when the override is enabled */
    bool ignore_rtc_persistence; /**< Start the cartridge RTC clean instead of loading its .rtc file */
    bool native_presentation_enabled; /**< Allow exact-ROM native hooks to replace guest presentation */
    GBHostConfiguration host_configuration; /**< Applied host-owned gameplay configuration and path-free identity */
} GBConfig;

/* ============================================================================
 * Debugging
 * ========================================================================== */

extern bool gbrt_trace_enabled;
extern bool gbrt_log_lcd_transitions;
extern bool gbrt_dispatch_fallback_tracking_enabled;
extern bool gbrt_rgb_framebuffer_enabled;
extern bool gbrt_benchmark_fast_tick_enabled;
extern bool gbrt_force_scalar_timer;
extern bool gbrt_force_eager_audio;
extern bool gbrt_visibility_estimator_enabled;
extern bool gbrt_test_breakpoint_enabled;
extern uint64_t gbrt_instruction_count;
extern uint64_t gbrt_instruction_limit;

extern void (*gbrt_instruction_limit_callback)(void);

typedef struct {
    uint8_t dpad;     /**< Active-low Right, Left, Up, Down bits */
    uint8_t buttons;  /**< Active-low A, B, Select, Start bits */
} GBJoypadState;

typedef enum {
    GB_EXECUTION_GENERATED = 0,
    GB_EXECUTION_INTERPRETER = 1,
} GBExecutionMode;

typedef struct {
    uint64_t max_steps;      /**< Number of scheduler steps to compare */
    uint64_t max_frames;     /**< Number of completed frames to compare (0 disables frame limit) */
    uint64_t log_interval;   /**< Progress log cadence (0 disables progress logs) */
    bool compare_memory;     /**< Compare mutable memory/PPU state on every step */
    bool log_fallbacks;      /**< Log generated-to-interpreter fallback events */
    bool fail_on_fallback;   /**< Treat generated-to-interpreter fallback as a mismatch */
    uint64_t inject_mismatch_step; /**< Test-only step whose interpreted A register is perturbed (0 disables) */
    const char* input_script;/**< Optional scripted input in frame:buttons:duration or c<cycle>:buttons:duration format */
} GBDifferentialOptions;

typedef struct {
    bool matched;            /**< True if both paths stayed in sync */
    uint64_t steps_completed;/**< Number of completed comparison steps */
    uint64_t frames_completed;/**< Number of completed frame boundaries */
    uint64_t mismatch_step;  /**< Step index of the first mismatch */
    uint16_t pc;             /**< PC at the start of the mismatching step */
    uint16_t bank;           /**< Active ROM bank at the start of the mismatching step */
    char message[256];       /**< Short mismatch description */
} GBDifferentialResult;

#define GBRT_INTERPRETER_HOTSPOT_CAPACITY 16
#define GBRT_DISPATCH_FALLBACK_SITE_CAPACITY 256

typedef enum {
    GB_DISPATCH_FALLBACK_ADDRESS_NOT_COMPILED = 1,
    GB_DISPATCH_FALLBACK_BANK_NOT_COMPILED = 2,
    GB_DISPATCH_FALLBACK_PAGE_NOT_COMPILED = 3,
    GB_DISPATCH_FALLBACK_WRITABLE_HRAM = 4,
} GBDispatchFallbackReason;

typedef struct {
    uint8_t valid;                    /**< Slot contains an observed site */
    GBDispatchFallbackReason reason;  /**< Generated dispatch failure class */
    uint16_t bank;                    /**< Runtime bank at the fallback entry */
    uint16_t addr;                    /**< Runtime PC at the fallback entry */
    uint16_t compiled_bank_variants;  /**< Compiled banks at this address */
    uint64_t entries;                 /**< Number of fallback handoffs */
    uint64_t instructions;            /**< Instructions executed by the fallback */
    uint64_t cycles;                  /**< Guest cycles executed by the fallback */
    uint64_t first_frame;             /**< First guest frame that hit this site */
    uint64_t last_frame;              /**< Most recent guest frame that hit this site */
} GBDispatchFallbackSite;

typedef struct {
    uint8_t valid;           /**< Slot contains a tracked hotspot */
    uint16_t bank;           /**< Bank of the fallback entry point */
    uint16_t addr;           /**< Address of the fallback entry point */
    uint64_t entries;        /**< Number of interpreter entries at this site */
    uint64_t instructions;   /**< Interpreted instructions attributed to this site */
    uint64_t cycles;         /**< Interpreted cycles attributed to this site */
    uint64_t last_frame;     /**< Most recent guest frame that hit this site */
} GBInterpreterHotspot;

/**
 * @brief Compile-time-gated counters for generated/runtime transition costs
 *
 * These counters are diagnostic state only. They are intentionally excluded
 * from savestates, deterministic state dumps, and differential comparisons.
 */
typedef struct {
    uint64_t tick_commits;                  /**< Calls that committed guest cycles through gb_tick() */
    uint64_t tick_cycles;                   /**< Guest CPU cycles supplied to gb_tick() */
    uint64_t generated_safepoints;          /**< Generated stop checks reached after timing work */
    uint64_t generated_direct_transitions;  /**< Non-inlined generated bodies entered */
    uint64_t generated_indirect_dispatches; /**< Address/bank dispatcher iterations */
    uint64_t generated_generic_reads;       /**< Generated reads that used the full runtime decoder */
    uint64_t generated_specialized_reads;   /**< Generated reads served by a proven direct range */
    uint64_t generated_generic_writes;      /**< Generated writes that used the full runtime decoder */
    uint64_t generated_specialized_writes;  /**< Generated writes served by a proven direct range */
    uint64_t interpreter_fallbacks;         /**< Generated dispatches that entered the interpreter */
    uint64_t ppu_tick_calls;                /**< Runtime-to-PPU synchronization calls */
    uint64_t ppu_dots;                      /**< LCD-enabled system cycles supplied to the PPU */
    uint64_t ppu_draw_dots;                 /**< Dot-granular Mode 3 iterations */
    uint64_t ppu_rendered_pixels;           /**< Visible pixels emitted by the dot renderer */
    uint64_t ppu_stable_spans;              /**< Proven multi-dot raster spans executed */
    uint64_t ppu_stable_span_dots;          /**< Dots covered by proven raster spans */
    uint64_t audio_samples;                 /**< Stereo sample frames produced by the APU */

#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    /* NL-0 activity attribution. Array labels are emitted by
     * gbrt_report_performance_counters(). */
    uint64_t tick_cycle_histogram[12];
    uint64_t ppu_mode_tick_commits[5];
    uint64_t ppu_mode_tick_cycles[5];
    uint64_t timer_state_tick_commits[3];
    uint64_t timer_state_tick_cycles[3];
    uint64_t deadline_histogram[10];
    uint64_t visibility_unit_histogram[7];
    uint64_t region_group_size_histogram[6];

    uint64_t rtc_tick_calls;
    uint64_t rtc_tick_cycles;
    uint64_t dma_tick_calls;
    uint64_t dma_tick_cycles;
    uint64_t serial_tick_calls;
    uint64_t serial_tick_cycles;
    uint64_t timer_tick_calls;
    uint64_t timer_tick_cycles;
    uint64_t audio_step_calls;
    uint64_t audio_step_cycles;
    uint64_t interrupt_checks;
    uint64_t interrupt_stops;
    uint64_t frame_stops;

    /* Conservative visibility-aware grouping estimate. */
    uint64_t region_candidate_units;
    uint64_t region_groups;
    uint64_t region_grouped_units;
    uint64_t region_grouped_tick_commits;
    uint64_t region_estimated_removable_tick_commits;
    uint64_t region_estimated_removable_safepoints;
    uint64_t region_reject_visibility;
    uint64_t region_reject_deadline;

    /* Live diagnostic-only estimator state. */
    uint64_t profile_unit_tick_commits;
    uint64_t profile_unit_tick_cycles;
    uint32_t profile_unit_visibility_mask;
    uint8_t profile_unit_safe_memory;
    uint64_t profile_group_units;
    uint64_t profile_group_tick_commits;
    uint64_t profile_group_cycles;
    uint32_t profile_group_deadline_remaining;
#endif
} GBPerformanceCounters;

/* ============================================================================
 * CPU Context
 * ========================================================================== */

/**
 * @brief Forward declaration
 */
typedef struct GBContext GBContext;

#ifndef GB_SEMANTIC_TRANSACTION_MAX_DIRTY_RANGES
#define GB_SEMANTIC_TRANSACTION_MAX_DIRTY_RANGES 16u
#endif

typedef enum GBSemanticTransactionOutcome {
    GB_SEMANTIC_TRANSACTION_NONE = 0,
    GB_SEMANTIC_TRANSACTION_COMMITTED = 1,
    GB_SEMANTIC_TRANSACTION_ABORTED = 2,
    GB_SEMANTIC_TRANSACTION_VALIDATION_FAILED = 3,
    GB_SEMANTIC_TRANSACTION_COMMIT_FAILED = 4,
} GBSemanticTransactionOutcome;

typedef struct GBSemanticTransactionRangeMetadata {
    uint8_t space;
    uint16_t bank;
    uint16_t address;
    uint32_t width;
} GBSemanticTransactionRangeMetadata;

/**
 * @brief Platform callbacks for I/O and rendering
 */
typedef struct {
    void (*on_vblank)(GBContext* ctx, const uint8_t* framebuffer);
    void (*on_audio_sample)(GBContext* ctx, int16_t left, int16_t right);
    uint8_t (*get_joypad)(GBContext* ctx);
    void (*on_serial_byte)(GBContext* ctx, uint8_t byte);
    
    /* Save Data / External RAM */
    bool (*load_battery_ram)(GBContext* ctx, const char* rom_name, void* data, size_t size);
    bool (*save_battery_ram)(GBContext* ctx, const char* rom_name, const void* data, size_t size);
    bool (*load_rtc_data)(GBContext* ctx, const char* rom_name, void* data, size_t size);
    bool (*save_rtc_data)(GBContext* ctx, const char* rom_name, const void* data, size_t size);
} GBPlatformCallbacks;

/**
 * @brief CPU register and state context
 * 
 * This structure is passed to all recompiled functions and contains
 * the current state of the emulated CPU.
 */
typedef struct GBContext {
    /* 8-bit registers */
    union {
        struct { uint8_t f, a; };  /**< AF register pair (little-endian) */
        uint16_t af;
    };
    union {
        struct { uint8_t c, b; };  /**< BC register pair */
        uint16_t bc;
    };
    union {
        struct { uint8_t e, d; };  /**< DE register pair */
        uint16_t de;
    };
    union {
        struct { uint8_t l, h; };  /**< HL register pair */
        uint16_t hl;
    };
    
    /* Stack pointer and program counter */
    uint16_t sp;
    uint16_t pc;
    
    /* Flag bits (unpacked for performance) */
    uint8_t f_z;  /**< Zero flag */
    uint8_t f_n;  /**< Subtract flag */
    uint8_t f_h;  /**< Half-carry flag */
    uint8_t f_c;  /**< Carry flag */
    
    /* Interrupt state */
    uint8_t ime;          /**< Interrupt Master Enable */
    uint8_t ime_pending;  /**< IME will be enabled after next instruction */
    uint8_t halted;       /**< CPU is halted */
    uint8_t stopped;      /**< Scheduler/execution slice requested to stop */
    uint8_t stop_mode_active; /**< CPU is in STOP low-power mode */
    uint8_t halt_bug;     /**< HALT bug: next instruction byte read twice */
    uint8_t single_step_mode; /**< Debug mode: execute at most one instruction */
    uint8_t cgb_double_speed; /**< CGB double-speed mode is enabled */
    uint8_t cgb_system_cycle_remainder; /**< Pending half system cycle in CGB double-speed mode */
    
    /* OAM DMA state */
    struct {
        uint8_t active;         /**< DMA is in progress (bus blocking) */
        uint8_t pending;        /**< DMA was requested, startup delay in progress */
        uint8_t source_high;    /**< Most recently written source address >> 8 */
        uint8_t active_source_high; /**< Source page owned by the active transfer */
        uint8_t progress;       /**< Bytes copied (0-159) */
        uint16_t cycles_remaining; /**< Cycles until DMA completes */
        uint8_t startup_delay;  /**< T-cycles before the new transfer replaces the old one */
    } dma;

    /* CGB HDMA state */
    struct {
        uint16_t source;        /**< Current source address */
        uint16_t dest;          /**< Current destination address (0x8000-0x9FF0) */
        uint8_t blocks_remaining; /**< Remaining 0x10-byte blocks */
        uint8_t active;         /**< Transfer is currently active */
        uint8_t hblank_mode;    /**< Transfer runs during HBlank only */
        uint16_t cpu_stall_cycles; /**< CPU cycles owed while hardware keeps running */
        uint8_t processing_stall; /**< Internal recursion guard while charging a stall */
    } hdma;
    
    /* Current bank numbers */
    uint16_t rom_bank;    /**< Current ROM bank (0x4000-0x7FFF) - 9 bits for MBC5 */
    uint8_t ram_bank;     /**< Current RAM bank */
    uint8_t wram_bank;    /**< Current WRAM bank (CGB only) */
    uint8_t vram_bank;    /**< Current VRAM bank (CGB only) */

    /* Runtime configuration */
    GBConfig config;
    char save_id[64];
    bool persistence_load_failed; /**< Invalid persisted data was rejected; automatic overwrite is suppressed */
    uint64_t semantic_transaction_sequence; /**< Monotonic host semantic transaction id */
    GBSemanticTransactionOutcome semantic_transaction_outcome; /**< Last completed transaction outcome */
    uint8_t semantic_transaction_dirty_count; /**< Number of replay-visible dirty ranges */
    GBSemanticTransactionRangeMetadata
        semantic_transaction_dirty[GB_SEMANTIC_TRANSACTION_MAX_DIRTY_RANGES];
    void* port_state; /**< Opaque optional native port/frontend state */
    GBHostConfigurationContract host_configuration_contract; /**< Host-only configuration validation service */
    const char* host_configuration_path; /**< Host-only persistence destination; never serialized or logged */
    
    /* MBC state */
    uint8_t mbc_type;
    uint8_t ram_enabled;
    uint8_t mbc_mode;       /**< Banking mode for MBC1 (0=ROM, 1=RAM/Advanced) */
    uint8_t rom_bank_low;   /**< Raw low ROM-bank register (MBC1/2/3/5) */
    uint8_t rom_bank_upper; /**< MBC1: Upper 2 bits of ROM bank / RAM bank selector */
    uint8_t mbc1_multicart; /**< MBC1M wiring detected from a secondary header */
    uint8_t rtc_mode;       /**< 0=RAM, 1=RTC registers (for 0xA000-0xBFFF) */
    uint8_t rtc_reg;        /**< Selected RTC register (0x08-0x0C) */
    
    /* Timing */
    uint32_t cycles;      /**< Cycles executed */
    uint64_t total_cycles;/**< Monotonic cycles executed, including 32-bit wraps */
    uint32_t frame_cycles;/**< Cycles this frame */
    uint32_t last_sync_cycles; /**< Last cycles count synchronized with hardware */
    uint32_t run_cycle_budget; /**< Active gb_run_cycles() slice budget, or 0 when unbounded */
    uint32_t run_cycle_budget_start; /**< Cycle counter at the start of the active slice budget */
    uint8_t  frame_done;  /**< Frame is finished and rendered */
    uint8_t  lcd_off_active; /**< LCDC bit 7 is currently off */
    uint32_t lcd_off_start_cycles; /**< Global cycle when the current LCD-off span started */
    uint32_t lcd_off_start_frame_cycles; /**< Frame-local cycle when the current LCD-off span started */
    uint32_t frame_lcd_off_cycles; /**< Cycles spent with LCD disabled in the current rendered frame */
    uint32_t frame_lcd_transition_count; /**< LCD on/off state changes seen in the current rendered frame */
    uint32_t frame_lcd_off_span_count; /**< LCD-off spans completed in the current rendered frame */
    uint32_t last_lcd_off_span_cycles; /**< Most recent LCD-off span length */
    uint64_t total_lcd_off_cycles; /**< Total cycles spent with LCD disabled */
    uint64_t total_lcd_transition_count; /**< Total LCD on/off state changes */
    uint64_t total_lcd_off_span_count; /**< Total completed LCD-off spans */
    
    /* Timer internal state */
    uint16_t div_counter;   /**< Internal 16-bit divider counter */
    uint8_t tima_reload_pending; /**< TIMA reload state: 1-4 delay, 0x81-0x84 reload M-cycle */

    /* Lazy APU scheduler state. Pending time is flushed at the next sample
     * deadline or before guest-visible APU access. */
    uint32_t audio_pending_cpu_cycles;
    uint32_t audio_cycles_until_event;
    uint16_t audio_pending_old_div;
    uint8_t audio_pending_system_cycle_remainder;
    uint8_t audio_pending_double_speed;

    /* Serial transfer state */
    struct {
        uint8_t active;      /**< Internal-clock transfer currently in progress */
        uint8_t fast_clock;  /**< CGB fast serial clock is selected */
        uint32_t cycles_remaining; /**< Remaining CPU cycles until completion */
    } serial_transfer;
    
    /* Memory pointers */
    uint8_t* rom;         /**< ROM data */
    size_t rom_size;
    void* data_mod_state; /**< Opaque immutable physical-ROM overlay state */
    uint8_t* eram;        /**< External RAM */
    size_t eram_size;
    uint8_t* wram;        /**< Work RAM */
    uint8_t* vram;        /**< Video RAM */
    uint8_t* oam;         /**< Object Attribute Memory */
    uint8_t* hram;        /**< High RAM (0xFF80-0xFFFE) */
    uint8_t* io;          /**< I/O registers (0xFF00-0xFF7F) */
    
    /* RTC state (MBC3) */
    struct {
        uint8_t s, m, h, dl, dh;        /**< Seconds, Minutes, Hours, Days Low, Days High */
        uint8_t latched_s, latched_m, latched_h, latched_dl, latched_dh;
        uint8_t latch_state;            /**< 0=Normal, 1=Latch prepared (wrote 0) */
        uint64_t last_time;             /**< Last time update (in cycles) */
        bool active;                    /**< RTC oscillator active (DH bit 6) */
    } rtc;
    
    /* Hardware components (opaque pointers) */
    void* ppu;            /**< Pixel Processing Unit */
    void* apu;            /**< Audio Processing Unit */
    void* timer;          /**< Timer unit */
    void* serial;         /**< Serial port */
    void* joypad;         /**< Joypad input */
    uint8_t last_joypad;  /**< Last joypad state for interrupt generation */
    uint8_t used_dispatch_fallback; /**< Generated path fell back to interpreter */
    uint16_t dispatch_fallback_bank; /**< Bank used for the most recent fallback */
    uint16_t dispatch_fallback_addr; /**< PC used for the most recent fallback */
    uint32_t frame_dispatch_fallbacks; /**< Fallback count accumulated in the current frame */
    uint64_t total_dispatch_fallbacks; /**< Total generated-to-interpreter fallbacks */
    uint16_t frame_first_fallback_bank; /**< First fallback bank in the current frame */
    uint16_t frame_first_fallback_addr; /**< First fallback PC in the current frame */
    uint16_t frame_last_fallback_bank; /**< Last fallback bank in the current frame */
    uint16_t frame_last_fallback_addr; /**< Last fallback PC in the current frame */
    uint64_t total_interpreter_entries; /**< Total interpreter sessions entered */
    uint64_t total_interpreter_instructions; /**< Total instructions executed in interpreter sessions */
    uint64_t total_interpreter_cycles; /**< Total cycles executed in interpreter sessions */
    uint64_t frame_interpreter_instructions; /**< Interpreter instructions in the current frame */
    uint64_t frame_interpreter_cycles; /**< Interpreter cycles in the current frame */
    uint8_t has_unimplemented_interpreter_opcode; /**< Interpreter hit an unsupported opcode */
    uint8_t last_unimplemented_opcode; /**< Most recent unsupported opcode seen by the interpreter */
    uint16_t last_unimplemented_bank; /**< Bank of the most recent unsupported opcode */
    uint16_t last_unimplemented_addr; /**< Address of the most recent unsupported opcode */
    GBInterpreterHotspot interpreter_hotspots[GBRT_INTERPRETER_HOTSPOT_CAPACITY]; /**< Top interpreter entry hotspots */
    GBDispatchFallbackSite dispatch_fallback_sites[GBRT_DISPATCH_FALLBACK_SITE_CAPACITY]; /**< Lossless diagnostic site inventory up to capacity */
    uint16_t dispatch_fallback_site_count; /**< Number of populated fallback sites */
    uint64_t dispatch_fallback_sites_dropped; /**< New sites omitted after capacity exhaustion */
    uint64_t completed_frames; /**< Number of completed guest frames */
    GBPerformanceCounters performance_counters; /**< Non-semantic profiling counters */
    
    /* Platform interface */
    void* platform;       /**< Platform-specific data */
    GBPlatformCallbacks callbacks; /**< Platform callbacks */
    
    /* Trace context */
    void* trace_file;     /**< FILE* for trace output */
    bool trace_entries_enabled;
    void* ppu_trace_file; /**< FILE* for focused PPU trace output */
#ifdef GBRT_ENABLE_NATIVE_PATCHES
    /* Opaque, lazily allocated per-context native replacement state. */
    void* native_patch_state;
#endif
} GBContext;

enum {
    GBRT_PROFILE_VISIBILITY_GENERIC_READ = 1u << 0,
    GBRT_PROFILE_VISIBILITY_GENERIC_WRITE = 1u << 1,
    GBRT_PROFILE_VISIBILITY_TRANSITION = 1u << 2,
    GBRT_PROFILE_VISIBILITY_FALLBACK = 1u << 3,
    GBRT_PROFILE_VISIBILITY_STOPPED = 1u << 4,
};

#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
void gbrt_profile_generated_safepoint(GBContext* ctx);
#endif

/**
 * Generated-code profiling hooks compile to no-ops when performance counters
 * are disabled. The stop predicate remains identical in either build mode.
 */
static inline bool gbrt_generated_safepoint(GBContext* ctx) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.generated_safepoints++;
    if (gbrt_visibility_estimator_enabled) {
        gbrt_profile_generated_safepoint(ctx);
    }
#endif
    return ctx->stopped != 0;
}

static inline void gbrt_note_generated_direct_transition(GBContext* ctx) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.generated_direct_transitions++;
    if (gbrt_visibility_estimator_enabled) {
        ctx->performance_counters.profile_unit_visibility_mask |=
            GBRT_PROFILE_VISIBILITY_TRANSITION;
    }
#else
    (void)ctx;
#endif
}

static inline void gbrt_note_generated_indirect_dispatch(GBContext* ctx) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.generated_indirect_dispatches++;
    if (gbrt_visibility_estimator_enabled) {
        ctx->performance_counters.profile_unit_visibility_mask |=
            GBRT_PROFILE_VISIBILITY_TRANSITION;
    }
#else
    (void)ctx;
#endif
}

static inline void gbrt_note_generated_generic_read(GBContext* ctx) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.generated_generic_reads++;
    if (gbrt_visibility_estimator_enabled) {
        ctx->performance_counters.profile_unit_visibility_mask |=
            GBRT_PROFILE_VISIBILITY_GENERIC_READ;
    }
#else
    (void)ctx;
#endif
}

static inline void gbrt_note_generated_specialized_read(GBContext* ctx) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.generated_specialized_reads++;
    if (gbrt_visibility_estimator_enabled) {
        ctx->performance_counters.profile_unit_safe_memory = 1u;
    }
#else
    (void)ctx;
#endif
}

static inline void gbrt_note_generated_generic_write(GBContext* ctx) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.generated_generic_writes++;
    if (gbrt_visibility_estimator_enabled) {
        ctx->performance_counters.profile_unit_visibility_mask |=
            GBRT_PROFILE_VISIBILITY_GENERIC_WRITE;
    }
#else
    (void)ctx;
#endif
}

static inline void gbrt_note_generated_specialized_write(GBContext* ctx) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.generated_specialized_writes++;
    if (gbrt_visibility_estimator_enabled) {
        ctx->performance_counters.profile_unit_safe_memory = 1u;
    }
#else
    (void)ctx;
#endif
}

static inline void gbrt_note_ppu_tick(GBContext* ctx, uint32_t dots) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.ppu_tick_calls++;
    ctx->performance_counters.ppu_dots += dots;
#else
    (void)ctx;
    (void)dots;
#endif
}

static inline void gbrt_note_ppu_draw_dot(GBContext* ctx, bool rendered_pixel) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.ppu_draw_dots++;
    if (rendered_pixel) {
        ctx->performance_counters.ppu_rendered_pixels++;
    }
#else
    (void)ctx;
    (void)rendered_pixel;
#endif
}

static inline void gbrt_note_ppu_draw_span(GBContext* ctx, uint32_t dots) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.ppu_draw_dots += dots;
    ctx->performance_counters.ppu_rendered_pixels += dots;
#else
    (void)ctx;
    (void)dots;
#endif
}

static inline void gbrt_note_ppu_stable_span(GBContext* ctx, uint32_t dots) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.ppu_stable_spans++;
    ctx->performance_counters.ppu_stable_span_dots += dots;
#else
    (void)ctx;
    (void)dots;
#endif
}

static inline void gbrt_note_audio_sample(GBContext* ctx) {
#ifdef GBRT_ENABLE_PERFORMANCE_COUNTERS
    ctx->performance_counters.audio_samples++;
#else
    (void)ctx;
#endif
}

/** @return true when this runtime was compiled with counter instrumentation. */
bool gbrt_performance_counters_available(void);

/** @brief Clear profiling counters without changing emulated machine state. */
void gbrt_reset_performance_counters(GBContext* ctx);

/** @brief Print one stable, machine-readable counter record to stderr. */
void gbrt_report_performance_counters(GBContext* ctx);

/* ============================================================================
 * Context Management
 * ========================================================================== */

/**
 * @brief Create a new GameBoy context
 * @param config Configuration settings
 * @return New context or NULL on failure
 */
GBContext* gb_context_create(const GBConfig* config);
void gb_context_set_host_configuration_service(
    GBContext* ctx,
    const GBHostConfigurationContract* contract,
    const char* path);

/**
 * @brief Destroy a GameBoy context
 * @param ctx Context to destroy
 */
void gb_context_destroy(GBContext* ctx);

/**
 * @brief Reset the CPU state
 * @param ctx Context to reset
 * @param skip_bootrom If true, initialize to post-bootrom state
 */
void gb_context_reset(GBContext* ctx, bool skip_bootrom);

/**
 * @brief Load a ROM into the context
 * @param ctx Target context
 * @param data ROM data
 * @param size ROM size in bytes
 * @return true on success
 */
bool gb_context_load_rom(GBContext* ctx, const uint8_t* data, size_t size);

/**
 * @brief Save battery-backed RAM to persistent storage
 * @param ctx Target context
 * @return true on success
 */
bool gb_context_save_ram(GBContext* ctx);

/**
 * @brief Persist only the supplied battery-RAM snapshot
 *
 * This does not save RTC state and does not mutate the context. The snapshot
 * must exactly match the cartridge's allocated external-RAM size.
 */
bool gb_context_save_battery_snapshot(
    GBContext* ctx,
    const uint8_t* data,
    size_t size);

/**
 * @brief Write a deterministic JSON snapshot for automated test oracles
 * @param ctx Target context
 * @param path Output path
 * @return true on success
 */
bool gb_context_write_state_json(const GBContext* ctx, const char* path);

/**
 * @brief Set a stable save identifier to use instead of the cartridge header title
 * @param ctx Target context
 * @param save_id Generated-project or ROM basename
 */
void gb_context_set_save_id(GBContext* ctx, const char* save_id);

/**
 * @brief Save a full emulator snapshot to a file
 * @param ctx Target context
 * @param path Output path
 * @return true on success
 */
bool gb_context_save_state_file(GBContext* ctx, const char* path);

/**
 * @brief Load a full emulator snapshot from a file
 * @param ctx Target context
 * @param path Input path
 * @return true on success
 */
bool gb_context_load_state_file(GBContext* ctx, const char* path);

/* ============================================================================
 * Memory Access
 * ========================================================================== */

/**
 * @brief Read a byte from memory
 * @param ctx CPU context
 * @param addr 16-bit address
 * @return Byte at address
 */
uint8_t gb_read8(GBContext* ctx, uint16_t addr);

/**
 * @brief Resolve a CPU ROM address to the physical 16 KiB ROM bank.
 * @param ctx CPU context
 * @param addr Address in 0x0000-0x7FFF
 * @return Physical bank after mapper rules and ROM-size wrapping
 */
uint16_t gb_resolve_rom_bank(const GBContext* ctx, uint16_t addr);

/**
 * @brief Write a byte to memory
 * @param ctx CPU context
 * @param addr 16-bit address
 * @param value Byte to write
 */
void gb_write8(GBContext* ctx, uint16_t addr, uint8_t value);

/**
 * @brief Read a 16-bit word from memory (little-endian)
 * @param ctx CPU context
 * @param addr 16-bit address
 * @return Word at address
 */
uint16_t gb_read16(GBContext* ctx, uint16_t addr);

/**
 * @brief Write a 16-bit word to memory (little-endian)
 * @param ctx CPU context
 * @param addr 16-bit address
 * @param value Word to write
 */
void gb_write16(GBContext* ctx, uint16_t addr, uint16_t value);

/* ============================================================================
 * Stack Operations
 * ========================================================================== */

/**
 * @brief Push a 16-bit value onto the stack
 */
void gb_push16(GBContext* ctx, uint16_t value);

/**
 * @brief Pop a 16-bit value from the stack
 */
uint16_t gb_pop16(GBContext* ctx);

/**
 * @brief Execute the bus phases of a PUSH rr instruction (16 T-cycles)
 *
 * The high byte is written first after the internal stack M-cycle, followed
 * by the low byte in the final M-cycle.
 */
void gbrt_timed_push16(GBContext* ctx, uint16_t value);

/**
 * @brief Execute the bus phases of a POP rr instruction (12 T-cycles)
 */
uint16_t gbrt_timed_pop16(GBContext* ctx);

/* ============================================================================
 * ALU Operations (with flag updates)
 * ========================================================================== */

void gb_add8(GBContext* ctx, uint8_t value);
void gb_adc8(GBContext* ctx, uint8_t value);
void gb_sub8(GBContext* ctx, uint8_t value);
void gb_sbc8(GBContext* ctx, uint8_t value);
void gb_and8(GBContext* ctx, uint8_t value);
void gb_or8(GBContext* ctx, uint8_t value);
void gb_xor8(GBContext* ctx, uint8_t value);
void gb_cp8(GBContext* ctx, uint8_t value);
uint8_t gb_inc8(GBContext* ctx, uint8_t value);
uint8_t gb_dec8(GBContext* ctx, uint8_t value);

void gb_add16(GBContext* ctx, uint16_t value);

/**
 * @brief Execute INC rr with its opcode and IDU machine cycles (8 T-cycles)
 *
 * On DMG hardware the second machine cycle exposes the original register
 * value on the address bus, which can trigger mode-2 OAM corruption.
 */
void gbrt_timed_inc16(GBContext* ctx, uint16_t* value);
void gbrt_timed_dec16(GBContext* ctx, uint16_t* value);

/**
 * @brief Execute LD A,(HL+/-) with its auto-index and data bus phase
 *
 * The original HL value is exposed during the data M-cycle while HL is
 * updated by @p delta. This sequencing is observable through the DMG OAM bug.
 */
uint8_t gbrt_timed_hl_read_auto(GBContext* ctx, int8_t delta);
void gbrt_timed_hl_write_auto(GBContext* ctx, uint8_t value, int8_t delta);

void gb_add_sp(GBContext* ctx, int8_t offset);
void gb_ld_hl_sp_n(GBContext* ctx, int8_t offset);

/**
 * @brief Execute ADD SP,e with its immediate read and idle M-cycles (16 T-cycles)
 * @param immediate_addr Address of the signed immediate byte
 */
void gbrt_timed_add_sp(GBContext* ctx, uint16_t immediate_addr);

/**
 * @brief Execute LD HL,SP+e with its immediate read and idle M-cycle (12 T-cycles)
 * @param immediate_addr Address of the signed immediate byte
 */
void gbrt_timed_ld_hl_sp_n(GBContext* ctx, uint16_t immediate_addr);

/* ============================================================================
 * Rotate/Shift Operations
 * ========================================================================== */

uint8_t gb_rlc(GBContext* ctx, uint8_t value);
uint8_t gb_rrc(GBContext* ctx, uint8_t value);
uint8_t gb_rl(GBContext* ctx, uint8_t value);
uint8_t gb_rr(GBContext* ctx, uint8_t value);
uint8_t gb_sla(GBContext* ctx, uint8_t value);
uint8_t gb_sra(GBContext* ctx, uint8_t value);
uint8_t gb_srl(GBContext* ctx, uint8_t value);
uint8_t gb_swap(GBContext* ctx, uint8_t value);

void gb_rlca(GBContext* ctx);
void gb_rrca(GBContext* ctx);
void gb_rla(GBContext* ctx);
void gb_rra(GBContext* ctx);

/* ============================================================================
 * Bit Operations
 * ========================================================================== */

void gb_bit(GBContext* ctx, uint8_t bit, uint8_t value);

/* ============================================================================
 * Misc Operations
 * ========================================================================== */

void gb_daa(GBContext* ctx);

/* ============================================================================
 * Control Flow
 * ========================================================================== */

/**
 * @brief Call a function at the given address
 */
void gb_call(GBContext* ctx, uint16_t addr);

/**
 * @brief Execute CALL bus phases and commit the destination (24 T-cycles)
 */
void gbrt_timed_call(GBContext* ctx,
                     uint16_t target,
                     uint16_t return_address);

/**
 * @brief Finish CALL after its opcode and two immediate reads (12 T-cycles)
 */
void gbrt_timed_call_after_imm16(GBContext* ctx,
                                 uint16_t target,
                                 uint16_t return_address);

/**
 * @brief Return from a function
 */
void gb_ret(GBContext* ctx);

/** @brief Execute RET bus phases (16 T-cycles). */
void gbrt_timed_ret(GBContext* ctx);

/** @brief Execute a taken conditional RET (20 T-cycles). */
void gbrt_timed_ret_cc(GBContext* ctx);

/** @brief Execute RETI bus phases and enable IME at retirement. */
void gbrt_timed_reti(GBContext* ctx);

/**
 * @brief RST vector call
 */
void gb_rst(GBContext* ctx, uint8_t vector);

/**
 * @brief Execute RST bus phases and commit the vector (16 T-cycles)
 */
void gbrt_timed_rst(GBContext* ctx,
                    uint8_t vector,
                    uint16_t return_address);

/**
 * @brief Commit a JP-family target at the end of its instruction timing
 *
 * Use 16 cycles for JP nn / taken JP cc, 12 for a not-taken JP cc, and 4
 * for JP HL.
 */
void gbrt_timed_jump(GBContext* ctx,
                     uint16_t target,
                     uint8_t instruction_cycles);

/**
 * @brief Jump to address in HL (JP HL)
 */
void gbrt_jump_hl(GBContext* ctx);

/**
 * @brief Fast-forward a tight visible-line LY polling loop.
 */
bool gbrt_fast_forward_visible_ly_wait(GBContext* ctx,
                                       uint8_t target_ly,
                                       uint32_t miss_iteration_cycles,
                                       uint32_t hit_iteration_cycles);

/**
 * @brief Dispatch to recompiled function at address
 */
void gb_dispatch(GBContext* ctx, uint16_t addr);

/**
 * @brief Dispatch a CALL to unanalyzed code (pushes return address first)
 */
void gb_dispatch_call(GBContext* ctx, uint16_t addr);

/**
 * @brief Fallback interpreter for uncompiled code
 */
void gb_interpret(GBContext* ctx, uint16_t addr);

/**
 * @brief Execute known copied HRAM helper stubs in-place when possible
 * @return 1 if a known HRAM stub instruction was executed, 0 otherwise
 */
uint8_t gbrt_try_execute_hram_stub(GBContext* ctx, uint16_t addr);

/**
 * @brief Execute simple copied helper stubs in the FF00-FF7F high-memory range
 * @return 1 if a known helper instruction was executed, 0 otherwise
 */
uint8_t gbrt_try_execute_highmem_stub(GBContext* ctx, uint16_t addr);

/**
 * @brief Execute simple copied RAM helper stubs in-place when possible
 * @return 1 if a known RAM stub instruction was executed, 0 otherwise
 */
uint8_t gbrt_try_execute_ram_stub(GBContext* ctx, uint16_t addr);

/* ============================================================================
 * CPU State
 * ========================================================================== */

/**
 * @brief Halt the CPU until interrupt
 */
void gb_halt(GBContext* ctx);

/**
 * @brief Retire HALT and enter either the halted state or the HALT-bug state
 *
 * With IME clear and an enabled interrupt already pending, the CPU remains
 * awake and suppresses the next opcode-fetch PC increment. Otherwise it
 * enters the normal halted state. The caller supplies the post-fetch PC and
 * all cycles accumulated through the HALT instruction.
 */
void gbrt_execute_halt(GBContext* ctx, uint16_t next_pc, uint32_t cycles);

/**
 * @brief Stop the CPU (and LCD)
 */
void gb_stop(GBContext* ctx);

/* ============================================================================
 * Flag Helpers
 * ========================================================================== */

/**
 * @brief Pack individual flags into F register
 */
static inline void gb_pack_flags(GBContext* ctx) {
    ctx->f = (ctx->f_z ? 0x80 : 0) |
             (ctx->f_n ? 0x40 : 0) |
             (ctx->f_h ? 0x20 : 0) |
             (ctx->f_c ? 0x10 : 0);
}



/**
 * @brief Unpack F register into individual flags
 */
static inline void gb_unpack_flags(GBContext* ctx) {
    ctx->f_z = (ctx->f & 0x80) != 0;
    ctx->f_n = (ctx->f & 0x40) != 0;
    ctx->f_h = (ctx->f & 0x20) != 0;
    ctx->f_c = (ctx->f & 0x10) != 0;
}

/* ============================================================================
 * Timing
 * ========================================================================== */

/**
 * @brief Add cycles to the timing counters
 */
void gb_add_cycles(GBContext* ctx, uint32_t cycles);

/**
 * @brief Run one HBlank HDMA block if a transfer is pending
 */
void gbrt_hdma_hblank(GBContext* ctx);

/**
 * @brief Check if a frame worth of cycles has elapsed
 */
bool gb_frame_complete(GBContext* ctx);

/**
 * @brief Get the current framebuffer
 * @param ctx CPU context
 * @return Pointer to 160x144 ARGB8888 framebuffer, or NULL if not ready
 */
const uint32_t* gb_get_framebuffer(GBContext* ctx);

/**
 * @brief Reset the frame ready flag for the next frame
 * @param ctx CPU context
 */
void gb_reset_frame(GBContext* ctx);

/**
 * @brief Process hardware for the given number of cycles
 */
void gb_tick(GBContext* ctx, uint32_t cycles);

/** Publish any lazily accumulated APU time before observing audio state. */
void gbrt_audio_sync(GBContext* ctx);

/**
 * @brief Return CPU T-cycles until the earliest batching-relevant boundary
 *
 * The deadline covers interrupt acceptance, delayed IME, the active run
 * budget, timer edges/reload, PPU state transitions, OAM DMA, serial transfer,
 * and outstanding HDMA stalls. UINT32_MAX means no modeled boundary is
 * pending; zero disables batching until the current boundary is published.
 */
uint32_t gbrt_cycles_until_next_event(const GBContext* ctx);

/**
 * @brief Read one byte after a precise number of leading T-cycles
 *
 * The helper consumes one additional T-cycle after sampling the bus, so the
 * complete phase lasts @p leading_cycles + 1 T-cycles.
 */
static inline uint8_t gbrt_timed_bus_read8(GBContext* ctx,
                                            uint16_t addr,
                                            uint8_t leading_cycles) {
    gb_tick(ctx, leading_cycles);
    const uint8_t value = gb_read8(ctx, addr);
    gb_tick(ctx, 1);
    return value;
}

/**
 * @brief Write one byte after a precise number of leading T-cycles
 *
 * The helper consumes one additional T-cycle after driving the bus, so the
 * complete phase lasts @p leading_cycles + 1 T-cycles.
 */
static inline void gbrt_timed_bus_write8(GBContext* ctx,
                                          uint16_t addr,
                                          uint8_t value,
                                          uint8_t leading_cycles) {
    gb_tick(ctx, leading_cycles);
    gb_write8(ctx, addr, value);
    gb_tick(ctx, 1);
}

/**
 * @brief Commit an RMW store at the start of its final machine cycle
 *
 * The preceding read phase has already retired through the end of the
 * penultimate M-cycle. The write is therefore visible immediately, followed
 * by the four T-cycles that retire the final M-cycle.
 */
static inline void gbrt_timed_bus_rmw_write8(GBContext* ctx,
                                              uint16_t addr,
                                              uint8_t value) {
    gb_write8(ctx, addr, value);
    gb_tick(ctx, 4);
}

/** @brief Service the highest-priority pending interrupt, if IME allows it. */
void gb_handle_interrupts(GBContext* ctx);

/* ============================================================================
 * Platform Interface
 * ========================================================================== */

/* Moved GBPlatformCallbacks definition to top to resolve circular dependency */

/**
 * @brief Set platform callbacks
 */
void gb_set_platform_callbacks(GBContext* ctx, const GBPlatformCallbacks* callbacks);

/* ============================================================================
 * Execution
 * ========================================================================== */

/**
 * @brief Run one frame of emulation
 * @return Number of cycles executed
 */
uint32_t gb_run_frame(GBContext* ctx);

/**
 * @brief Run emulation until a frame completes or the cycle budget is spent
 * @param max_cycles Maximum cycles to execute before returning (0 = no limit)
 * @return Number of cycles executed
 */
uint32_t gb_run_cycles(GBContext* ctx, uint32_t max_cycles);

/**
 * @brief Run a single step (one instruction or until interrupt)
 * @return Number of cycles executed
 */
uint32_t gb_step(GBContext* ctx);

/**
 * @brief Run one scheduler step in a specific execution mode
 * @param mode Generated dispatch or interpreter execution
 * @return Number of cycles executed
 */
uint32_t gb_debug_step(GBContext* ctx, GBExecutionMode mode);

/**
 * @brief Compare generated and interpreter execution in lockstep
 * @return true if both paths stayed in sync for the full run
 */
bool gb_run_differential(GBContext* generated_ctx,
                         GBContext* interpreted_ctx,
                         const GBDifferentialOptions* options,
                         GBDifferentialResult* result);

/**
 * @brief Record a generated-dispatch fallback into the interpreter
 */
void gbrt_note_dispatch_fallback(GBContext* ctx, uint16_t bank, uint16_t addr);

/**
 * @brief Execute and attribute one generated-dispatch fallback
 */
void gbrt_execute_dispatch_fallback(GBContext* ctx,
                                    uint16_t bank,
                                    uint16_t addr,
                                    GBDispatchFallbackReason reason,
                                    uint16_t compiled_bank_variants);

/**
 * @brief Return the stable diagnostic name for a fallback reason
 */
const char* gbrt_dispatch_fallback_reason_name(GBDispatchFallbackReason reason);

/**
 * @brief Record one completed interpreter session for hotspot tracking
 */
void gbrt_note_interpreter_session(GBContext* ctx,
                                   uint16_t bank,
                                   uint16_t addr,
                                   uint32_t instructions,
                                   uint32_t cycles);

/**
 * @brief Record an unsupported opcode observed by the interpreter
 */
void gbrt_note_unimplemented_interpreter_opcode(GBContext* ctx,
                                                uint16_t bank,
                                                uint16_t addr,
                                                uint8_t opcode);

/**
 * @brief Helper to invoke audio callback
 */
void gb_audio_callback(GBContext* ctx, int16_t left, int16_t right);

/**
 * @brief Set input automation script.
 *
 * Legacy entries use "frame:buttons:duration". Cycle-anchored entries use
 * "c<cycle>:buttons:<duration_cycles>".
 */
/**
 * @return true when the complete script is valid and installed. A malformed
 * script is rejected atomically and leaves no scripted input installed.
 */
bool gb_platform_set_input_script(const char* script);

/**
 * @brief Override the directory used for battery, RTC, and save-state files.
 * @return true when path is an existing directory. NULL or empty clears it.
 */
bool gb_platform_set_persistence_dir(const char* path);

/**
 * @brief Record live keyboard/controller input to a replayable script file
 */
void gb_platform_set_input_record_file(const char* path);

/**
 * @brief Set frames to dump screenshots (format: "frame1,frame2,...")
 */
void gb_platform_set_dump_frames(const char* frames);

/**
 * @brief Set filename prefix for screenshots
 */
void gb_platform_set_screenshot_prefix(const char* prefix);

/**
 * @brief Enable entry tracing to a file
 */
void gbrt_set_trace_file(const char* filename);

/**
 * @brief Log an entry point to the trace file
 */
void gbrt_log_trace(GBContext* ctx, uint16_t bank, uint16_t addr);
void gbrt_log_ppu_scanline(GBContext* ctx,
                           uint8_t ly,
                           uint8_t mode,
                           uint8_t lcdc,
                           uint8_t stat,
                           uint8_t scx,
                           uint8_t scy,
                           uint8_t wx,
                           uint8_t wy,
                           uint8_t bgp,
                           uint8_t obp0,
                           uint8_t obp1,
                           uint8_t window_line,
                           bool window_triggered);
void gbrt_log_ppu_register_write(GBContext* ctx,
                                 uint16_t addr,
                                 uint8_t old_value,
                                 uint8_t new_value,
                                 uint8_t ly,
                                 uint8_t mode);
void gbrt_log_oam_snapshot(GBContext* ctx, const char* reason);
void gbrt_log_stat_irq_check(GBContext* ctx,
                             const char* reason,
                             uint8_t ly,
                             uint8_t mode,
                             uint8_t stat,
                             uint8_t source_state_mask,
                             uint8_t source_enable_mask,
                             uint8_t active_source_mask,
                             bool previous_line_state,
                             bool current_line_state);
void gbrt_log_stat_irq_request(GBContext* ctx,
                               const char* reason,
                               uint8_t ly,
                               uint8_t mode,
                               uint8_t stat,
                               uint8_t active_source_mask,
                               uint8_t if_before,
                               uint8_t if_after);
void gbrt_log_interrupt_service(GBContext* ctx,
                                const char* name,
                                uint16_t vector,
                                uint8_t if_before,
                                uint8_t ie_reg,
                                uint8_t interrupt_bit,
                                uint16_t pc_before,
                                uint16_t sp_before);
void gbrt_note_lcd_transition(GBContext* ctx, bool lcd_enabled, uint8_t old_lcdc, uint8_t new_lcdc, uint8_t ly, uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* GBRT_H */
