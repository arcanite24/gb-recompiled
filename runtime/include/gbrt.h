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
    bool enable_bootrom;
    bool enable_audio;
    bool enable_serial;
    uint32_t speed_percent; /**< 100 = normal, 200 = 2x, etc */
} GBConfig;

/* ============================================================================
 * CPU Context
 * ========================================================================== */

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
    uint8_t stopped;      /**< CPU is stopped */
    
    /* Current bank numbers */
    uint8_t rom_bank;     /**< Current ROM bank (0x4000-0x7FFF) */
    uint8_t ram_bank;     /**< Current RAM bank */
    uint8_t wram_bank;    /**< Current WRAM bank (CGB only) */
    uint8_t vram_bank;    /**< Current VRAM bank (CGB only) */
    
    /* MBC state */
    uint8_t mbc_type;
    uint8_t ram_enabled;
    uint8_t mbc_mode;     /**< Banking mode for MBC1 */
    
    /* Timing */
    uint32_t cycles;      /**< Cycles executed */
    uint32_t frame_cycles;/**< Cycles this frame */
    
    /* Memory pointers */
    uint8_t* rom;         /**< ROM data */
    size_t rom_size;
    uint8_t* eram;        /**< External RAM */
    size_t eram_size;
    uint8_t* wram;        /**< Work RAM */
    uint8_t* vram;        /**< Video RAM */
    uint8_t* oam;         /**< Object Attribute Memory */
    uint8_t* hram;        /**< High RAM (0xFF80-0xFFFE) */
    uint8_t* io;          /**< I/O registers (0xFF00-0xFF7F) */
    
    /* Hardware components (opaque pointers) */
    void* ppu;            /**< Pixel Processing Unit */
    void* apu;            /**< Audio Processing Unit */
    void* timer;          /**< Timer unit */
    void* serial;         /**< Serial port */
    void* joypad;         /**< Joypad input */
    
    /* Platform interface */
    void* platform;       /**< Platform-specific data */
    
} GBContext;

/* ============================================================================
 * Context Management
 * ========================================================================== */

/**
 * @brief Create a new GameBoy context
 * @param config Configuration settings
 * @return New context or NULL on failure
 */
GBContext* gb_context_create(const GBConfig* config);

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
void gb_add_sp(GBContext* ctx, int8_t offset);

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
 * @brief Return from a function
 */
void gb_ret(GBContext* ctx);

/**
 * @brief RST vector call
 */
void gb_rst(GBContext* ctx, uint8_t vector);

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

/* ============================================================================
 * CPU State
 * ========================================================================== */

/**
 * @brief Halt the CPU until interrupt
 */
void gb_halt(GBContext* ctx);

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

/* ============================================================================
 * Platform Interface
 * ========================================================================== */

/**
 * @brief Platform callbacks for I/O and rendering
 */
typedef struct {
    void (*on_vblank)(GBContext* ctx, const uint8_t* framebuffer);
    void (*on_audio_sample)(GBContext* ctx, int16_t left, int16_t right);
    uint8_t (*get_joypad)(GBContext* ctx);
    void (*on_serial_byte)(GBContext* ctx, uint8_t byte);
} GBPlatformCallbacks;

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
 * @brief Run a single step (one instruction or until interrupt)
 * @return Number of cycles executed
 */
uint32_t gb_step(GBContext* ctx);

#ifdef __cplusplus
}
#endif

#endif /* GBRT_H */
