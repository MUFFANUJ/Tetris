/* =============================================================================
 * main.c  —  Tetris Phase 1 — Complete Interactive Terminal Game
 * =============================================================================
 *
 * PROJECT: Tetris OS Simulator — Track A (Interactive Terminal Application)
 * PHASE  : Phase 1  — Library Integration & Basic Mechanics
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │               7 OS MODULE COVERAGE MAP                                  │
 * ├──────────────────────────────┬──────────────────────────────────────────┤
 * │ 1. Process Management        │ game_loop() controls which "process"     │
 * │                              │ (input / physics / render) runs each     │
 * │                              │ frame and in what order.                 │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 2. Memory Management         │ t_alloc / t_dealloc via memory.c.        │
 * │                              │ Board, piece, and score record are all   │
 * │                              │ dynamically allocated from virtual RAM.  │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 3. File System               │ score_save / score_load persist the      │
 * │                              │ high score between sessions using the    │
 * │                              │ allowed <stdio.h> file I/O calls.        │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 4. I/O Management            │ keyboard.c (input) + screen.c (output).  │
 * │                              │ Non-blocking keyPressed() drives the     │
 * │                              │ real-time loop; screen_* renders frames. │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 5. Error Handling & Security │ Every alloc is NULL-checked; out-of-     │
 * │                              │ bounds moves are rejected via            │
 * │                              │ t_in_bounds(); game resets cleanly on    │
 * │                              │ game-over instead of crashing.           │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 6. Networking                │ High-score board shows "SOLO MODE" label │
 * │                              │ with a stub for future multiplayer       │
 * │                              │ (architecture is modular / extensible).  │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 7. User Interface            │ Full ANSI colored board, HUD panel,      │
 * │                              │ controls legend, game-over screen all    │
 * │                              │ drawn exclusively via screen.c.          │
 * └──────────────────────────────┴──────────────────────────────────────────┘
 *
 * CONTROLS:
 *   A / ← — move piece left
 *   D / → — move piece right
 *   S / ↓ — soft drop (speed down)
 *   W / ↑ — rotate piece clockwise
 *   Space  — hard drop (instant place)
 *   Q      — quit game
 *
 * BUILD:
 *   make          (uses provided Makefile)
 *   ./tetris_os
 *
 * RULES COMPLIANCE:
 *   - No <string.h>, <math.h>, or direct malloc/free in game logic.
 *   - <stdio.h> used ONLY for file I/O (score persistence) and terminal I/O.
 *   - <stdlib.h> used ONLY in memory.c (one malloc) and keyboard.c (stty).
 *   - printf / scanf : NOT used anywhere. All output goes via screen.c.
 * ============================================================================= */

#include "../include/memory.h"
#include "../include/t_math.h"
#include "../include/t_string.h"
#include "../include/keyboard.h"
#include "../include/screen.h"
#include <stdio.h>    /* FILE, fopen, fclose, fscanf, fprintf — score file I/O */

/* Simple random number generator state (no stdlib rand/time) */
static int rand_state = 1;
static int rand_seeded = 0;

static void seed_random(int seed) {
    rand_state = (seed > 0) ? seed : 1;
    rand_seeded = 1;
}

static int get_random(int max) {
    if (max <= 0) return 0;
    rand_state = t_mod(t_mul(rand_state, 1103) + 12345, 32768);
    return t_mod(rand_state, max);
}

/* =============================================================================
 * SECTION 1: CONSTANTS & CONFIGURATION
 * ============================================================================= */

#define BOARD_W         10          /* playfield columns (standard Tetris)      */
#define BOARD_H         20          /* playfield visible rows                    */
#define BOARD_HIDDEN     4          /* extra rows above visible area (spawn zone)*/
#define BOARD_TOTAL_H   (BOARD_H + BOARD_HIDDEN)

/* Terminal layout anchors — these are display coordinates, not game logic values.
   Changing them shifts the board's screen position; game rules are unaffected. */
#define BOARD_ORIGIN_X   4          /* terminal column of the left border        */
#define BOARD_ORIGIN_Y   3          /* terminal row of the top border            */

/* Each board cell is drawn as two characters ("[]" or "  ") */
#define CELL_W           2

/* HUD panel position (right of the board) */
#define HUD_X           (BOARD_ORIGIN_X + (BOARD_W * CELL_W) + 4)
#define HUD_Y            3

/* Score / level constants */
#define SCORE_SINGLE    100
#define SCORE_DOUBLE    300
#define SCORE_TRIPLE    500
#define SCORE_TETRIS   800

/* Drop speed: loop iterations between automatic drops (lower = faster) */
#define SPEED_INITIAL  300000
#define SPEED_MIN       40000
#define SPEED_DECREMENT 20000       /* speed increase per level                 */

/* High-score file — uses allowed stdio file I/O */
#define SCORE_FILE      "highscore.txt"

/* Total virtual RAM given to our memory manager (1 MB) */
#define VIRTUAL_RAM_SIZE (1024 * 1024)

/* =============================================================================
 * SECTION 2: TETROMINO DEFINITIONS
 * ============================================================================= */

/*
 * Each tetromino is defined as a 4×4 bitmask for each of its 4 rotations.
 * A '1' bit means the cell is occupied.  Bits are row-major, left-to-right,
 * top-to-bottom within a 4×4 grid.
 *
 * Piece indices:
 *   0=I  1=O  2=T  3=S  4=Z  5=J  6=L
 */
#define NUM_PIECES    7
#define NUM_ROTATIONS 4
#define PIECE_SIZE    4

/* 4×4 binary bitmasks for all 7 pieces × 4 rotations */
static const int PIECES[NUM_PIECES][NUM_ROTATIONS][PIECE_SIZE][PIECE_SIZE] = {
    /* 0 — I piece (cyan) */
    {
        {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
        {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}
    },
    /* 1 — O piece (yellow) */
    {
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}
    },
    /* 2 — T piece (magenta) */
    {
        {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    /* 3 — S piece (green) */
    {
        {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
        {{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    /* 4 — Z piece (red) */
    {
        {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}
    },
    /* 5 — J piece (blue) */
    {
        {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}
    },
    /* 6 — L piece (bright yellow / orange) */
    {
        {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}
    }
};

/* ANSI foreground color for each piece type (matches classic Tetris colors) */
static const int PIECE_COLORS[NUM_PIECES] = {
    SCREEN_COLOR_CYAN,    /* I */
    SCREEN_COLOR_YELLOW,  /* O */
    SCREEN_COLOR_MAGENTA, /* T */
    SCREEN_COLOR_GREEN,   /* S */
    SCREEN_COLOR_RED,     /* Z */
    SCREEN_COLOR_BLUE,    /* J */
    SCREEN_COLOR_BRIGHT_YELLOW  /* L */
};

/* =============================================================================
 * SECTION 3: DATA STRUCTURES  (OS Module: Memory Management)
 * ============================================================================= */

/*
 * GameBoard — the 20×10 (plus hidden rows) playfield.
 *
 * board[row][col] == 0  → empty cell
 * board[row][col] == N  → filled by piece type (N-1), so 1=I, 2=O … 7=L
 * This encoding lets us remember which color to draw after locking.
 *
 * Allocated from virtual RAM via t_alloc at game start.
 */
typedef struct {
    int cells[BOARD_TOTAL_H][BOARD_W];
} GameBoard;

/*
 * Piece — the currently falling tetromino and its ghost (shadow).
 *
 * type     : piece index 0–6
 * rotation : 0–3
 * col, row : top-left of the 4×4 bounding box (board coordinates)
 *
 * Allocated from virtual RAM via t_alloc when a new piece spawns.
 */
typedef struct {
    int type;
    int rotation;
    int col;
    int row;
} Piece;

/*
 * GameState — top-level game context (score, level, stats).
 * Allocated from virtual RAM at startup.
 */
typedef struct {
    int  score;
    int  high_score;
    int  level;
    int  lines_cleared;
    int  running;           /* 1 = playing, 0 = quit requested */
    int  game_over;         /* 1 = piece spawned into filled cells */
    int  next_type;         /* preview piece type */
    int  drop_counter;      /* counts loop iterations for auto-drop timing */
    int  drop_speed;        /* iterations per auto-drop (decreases with level) */
    int  soft_drop_active;  /* 1 when S is held */
} GameState;

/* =============================================================================
 * SECTION 4: MODULE 3 — FILE SYSTEM  (High-Score Persistence)
 * ============================================================================= */

/*
 * score_load()
 *   Reads the previous high score from SCORE_FILE.
 *   Uses the allowed <stdio.h> file I/O (not printf/scanf).
 *   Returns 0 if the file doesn't exist (first run).
 *
 *   OS Module: File System — stores state that survives program exit.
 */
static int score_load(void) {
    FILE *f = fopen(SCORE_FILE, "r");
    if (!f) return 0;

    char buf[16];
    int  i = 0;
    int  c;

    /* Read the saved line into a buffer, then parse with our string library */
    while (i < (int)sizeof(buf) - 1 && (c = fgetc(f)) != EOF && c != '\n') {
        buf[i++] = (char)c;
    }
    buf[i] = '\0';
    fclose(f);

    return t_atoi(buf);   /* OS Module: String Library — no manual digit parsing */
}

/*
 * score_save(score)
 *   Writes `score` to SCORE_FILE using fputc — no fprintf.
 *   Overwrites previous contents.
 *
 *   Error handling: if fopen fails (e.g. read-only filesystem) the game
 *   continues; the high score simply isn't persisted this session.
 */
static void score_save(int score) {
    FILE *f = fopen(SCORE_FILE, "w");
    if (!f) return;               /* error handling: can't write → skip */

    char buf[16];
    t_itoa(score, buf);           /* convert int → string using our library */

    int i = 0;
    while (buf[i] != '\0') {
        fputc(buf[i], f);
        i++;
    }
    fputc('\n', f);
    fclose(f);
}

/* =============================================================================
 * SECTION 5: MODULE 5 — ERROR HANDLING (Board & Piece Helpers)
 * ============================================================================= */

/*
 * board_cell_filled(board, row, col)
 *   Returns 1 if (row, col) is outside the board boundaries OR if the cell
 *   at that position is already locked (non-zero).
 *
 *   OS Module: Error Handling — all collision checks route through here so
 *   there is a single, auditable place for boundary validation.
 *   Uses t_in_bounds() from our math library.
 */
static int board_cell_filled(const GameBoard *board, int row, int col) {
    /* Boundary check using custom math library (spec requirement) */
    if (!t_in_bounds(col, 0, BOARD_W - 1)) return 1;    /* out of bounds = blocked */
    if (!t_in_bounds(row, 0, BOARD_TOTAL_H - 1)) return 1;
    return (board->cells[row][col] != 0);
}

/*
 * piece_collides(board, piece, row_offset, col_offset, rotation)
 *   Tests whether a piece at (piece->row + row_offset, piece->col + col_offset)
 *   with the given rotation would collide with anything on the board.
 *
 *   Returns 1 (collision) or 0 (clear).
 *
 *   Used by: move_left, move_right, move_down, rotate, hard_drop.
 */
static int piece_collides(const GameBoard *board, const Piece *piece,
                          int row_off, int col_off, int rotation) {
    int pr, pc;
    for (pr = 0; pr < PIECE_SIZE; pr++) {
        for (pc = 0; pc < PIECE_SIZE; pc++) {
            if (PIECES[piece->type][rotation][pr][pc]) {
                int br = piece->row + pr + row_off;
                int bc = piece->col + pc + col_off;
                if (board_cell_filled(board, br, bc)) return 1;
            }
        }
    }
    return 0;
}

/*
 * ghost_row(board, piece)
 *   Calculates the lowest row the current piece can reach if dropped straight
 *   down — this is the "ghost" / shadow row shown as a guide.
 *
 *   OS Module: Error Handling — iterative descent with collision check
 *   ensures we never try to place below the board floor.
 */
static int ghost_row(const GameBoard *board, const Piece *piece) {
    int drop = 0;
    while (!piece_collides(board, piece, drop + 1, 0, piece->rotation)) {
        drop++;
    }
    return piece->row + drop;
}

/* =============================================================================
 * SECTION 6: MODULE 2 — MEMORY MANAGEMENT (Spawn / Lock / Clear)
 * ============================================================================= */

/*
 * piece_spawn(state)
 *   Allocates a new falling Piece from virtual RAM, sets its type to the
 *   previewed next_type, and positions it at the top-centre of the board.
 *   Picks a new next_type using a small custom RNG (no <stdlib.h> rand()).
 *
 *   Returns: pointer to the new Piece (must be t_dealloc'd when locked).
 *   Returns NULL if out of virtual memory (critical error → game over).
 *
 *   OS Module: Memory Management — dynamic allocation tracked; zero leaks.
 */
static Piece *piece_spawn(GameState *state) {
    Piece *p = (Piece *)t_alloc((int)sizeof(Piece));
    if (!p) return NULL;    /* error handling: OOM → cannot spawn → game over */

    p->type     = state->next_type;
    p->rotation = 0;
    p->col      = t_div(BOARD_W, 2) - 2;   /* centre the 4-wide bounding box */
    p->row      = BOARD_HIDDEN - 2;         /* just above visible area         */

    /* Next piece: random selection */
    state->next_type = get_random(NUM_PIECES);

    return p;
}

/*
 * piece_lock(board, piece, state)
 *   Copies the piece's cells onto the board (locking it in place).
 *   Frees the Piece allocation from virtual RAM.
 *   Stores the piece type+1 in each cell so the color can be recalled.
 *
 *   OS Module: Memory Management — matching dealloc for every alloc.
 */
static void piece_lock(GameBoard *board, Piece *piece, GameState *state) {
    int pr, pc;
    for (pr = 0; pr < PIECE_SIZE; pr++) {
        for (pc = 0; pc < PIECE_SIZE; pc++) {
            if (PIECES[piece->type][piece->rotation][pr][pc]) {
                int br = piece->row + pr;
                int bc = piece->col + pc;
                if (t_in_bounds(br, 0, BOARD_TOTAL_H - 1) &&
                    t_in_bounds(bc, 0, BOARD_W - 1)) {
                    board->cells[br][bc] = piece->type + 1; /* 1-based color index */
                }
            }
        }
    }
    t_dealloc(piece);   /* OS Module: Memory — free now that it's locked */
    (void)state;
}

/*
 * board_clear_lines(board, state)
 *   Scans the board for full rows, removes them, and shifts everything above
 *   down.  Updates score and level in GameState.
 *
 *   Scoring (standard Tetris):
 *     1 line = 100,  2 = 300,  3 = 500,  4 (Tetris) = 800  × level
 *
 *   Uses only custom math (t_mul) and no standard library calls.
 */
static void board_clear_lines(GameBoard *board, GameState *state) {
    int cleared = 0;
    int row;

    for (row = BOARD_TOTAL_H - 1; row >= 0; row--) {
        /* Check if this row is completely filled */
        int full = 1;
        int col;
        for (col = 0; col < BOARD_W; col++) {
            if (board->cells[row][col] == 0) { full = 0; break; }
        }

        if (full) {
            cleared++;
            /* Shift all rows above this one down by one */
            int r;
            for (r = row; r > 0; r--) {
                for (col = 0; col < BOARD_W; col++) {
                    board->cells[r][col] = board->cells[r - 1][col];
                }
            }
            /* Clear the top row */
            for (col = 0; col < BOARD_W; col++) {
                board->cells[0][col] = 0;
            }
            row++;  /* re-check the same row index (now filled with row above) */
        }
    }

    if (cleared == 0) return;

    /* Score multiplier table: 1→100, 2→300, 3→500, 4→800 */
    int base;
    if      (cleared == 1) base = SCORE_SINGLE;
    else if (cleared == 2) base = SCORE_DOUBLE;
    else if (cleared == 3) base = SCORE_TRIPLE;
    else                   base = SCORE_TETRIS;

    state->score        += t_mul(base, state->level);   /* custom math: multiply */
    state->lines_cleared += cleared;

    /* Level up every 10 lines */
    state->level = t_div(state->lines_cleared, 10) + 1;

    /* Increase drop speed as level rises; clamp at minimum */
    int new_speed = SPEED_INITIAL - t_mul(state->level - 1, SPEED_DECREMENT);
    state->drop_speed = t_max(new_speed, SPEED_MIN);    /* custom math: clamp   */

    /* Update high score */
    if (state->score > state->high_score) {
        state->high_score = state->score;
    }
}

/* =============================================================================
 * SECTION 7: MODULE 1 — PROCESS MANAGEMENT (Game Actions)
 * ============================================================================= */

/*
 * The following action functions represent the "processes" the game loop
 * schedules each frame.  process_input → process_physics → process_render.
 *
 * OS Module: Process Management — the game loop acts as a scheduler,
 * giving CPU time to each sub-process in a deterministic round-robin order.
 */

/* Move piece left if no collision */
static void action_move_left(GameBoard *board, Piece *piece) {
    if (!piece_collides(board, piece, 0, -1, piece->rotation)) {
        piece->col--;
    }
}

/* Move piece right if no collision */
static void action_move_right(GameBoard *board, Piece *piece) {
    if (!piece_collides(board, piece, 0, 1, piece->rotation)) {
        piece->col++;
    }
}

/*
 * action_rotate(board, piece)
 *   Rotates the piece clockwise (next rotation index mod 4).
 *   Applies a simple wall-kick: if the rotated position collides, try
 *   shifting 1 left, then 1 right before giving up.
 */
static void action_rotate(GameBoard *board, Piece *piece) {
    int next_rot = t_mod(piece->rotation + 1, NUM_ROTATIONS);

    if (!piece_collides(board, piece, 0, 0, next_rot)) {
        piece->rotation = next_rot;
    } else if (!piece_collides(board, piece, 0, -1, next_rot)) {
        /* Wall kick: shift left by 1 */
        piece->col--;
        piece->rotation = next_rot;
    } else if (!piece_collides(board, piece, 0, 1, next_rot)) {
        /* Wall kick: shift right by 1 */
        piece->col++;
        piece->rotation = next_rot;
    }
    /* If all three fail, rotation is blocked — piece stays as-is */
}

/*
 * action_hard_drop(board, piece, state)
 *   Instantly drops the piece to its ghost position, locks it, and awards
 *   2 points per row dropped (standard Tetris scoring).
 *   Returns 1 to signal the caller that a new piece must be spawned.
 */
static int action_hard_drop(GameBoard *board, Piece *piece, GameState *state) {
    int rows_dropped = 0;
    while (!piece_collides(board, piece, 1, 0, piece->rotation)) {
        piece->row++;
        rows_dropped++;
    }
    state->score += t_mul(2, rows_dropped);
    piece_lock(board, piece, state);  /* piece freed inside here */
    return 1;   /* signal: spawn next piece */
}

/* =============================================================================
 * SECTION 8: MODULE 4 — I/O + MODULE 7 — USER INTERFACE (Rendering)
 * ============================================================================= */

/*
 * render_border()
 *   Draws the static border around the Tetris playfield.
 *   Called once per frame before the board contents.
 */
static void render_border(void) {
    int row, col;

    screen_set_color(SCREEN_COLOR_WHITE, SCREEN_COLOR_DEFAULT);

    /* Top border */
    screen_set_cursor(BOARD_ORIGIN_X - 1, BOARD_ORIGIN_Y - 1);
    screen_render_char('+');
    for (col = 0; col < BOARD_W; col++) {
        screen_render_string("--");
    }
    screen_render_char('+');

    /* Side borders for each visible row */
    for (row = 0; row < BOARD_H; row++) {
        int term_row = BOARD_ORIGIN_Y + row;
        screen_set_cursor(BOARD_ORIGIN_X - 1, term_row);
        screen_render_char('|');
        screen_set_cursor(BOARD_ORIGIN_X + t_mul(BOARD_W, CELL_W), term_row);
        screen_render_char('|');
    }

    /* Bottom border */
    screen_set_cursor(BOARD_ORIGIN_X - 1, BOARD_ORIGIN_Y + BOARD_H);
    screen_render_char('+');
    for (col = 0; col < BOARD_W; col++) {
        screen_render_string("--");
    }
    screen_render_char('+');

    screen_reset_color();
}

/*
 * render_cell(term_col, term_row, color_code)
 *   Draws a single filled cell ("[]") in the given ANSI color.
 *   Draws empty space ("  ") when color_code == 0.
 */
static void render_cell(int term_col, int term_row, int color_code) {
    screen_set_cursor(term_col, term_row);
    if (color_code == 0) {
        screen_render_string("  ");     /* empty cell */
    } else {
        screen_set_color(color_code, SCREEN_COLOR_DEFAULT);
        screen_render_string("[]");     /* filled cell */
        screen_reset_color();
    }
}

/*
 * render_board(board, piece)
 *   Renders the locked board contents, then overlays the ghost (shadow)
 *   and the live falling piece.
 *
 *   Draw order: board → ghost → active piece
 *   (each layer overwrites the previous for the same cells)
 *
 *   OS Module: User Interface — all drawing goes through screen.c helpers.
 */
static void render_board(const GameBoard *board, const Piece *piece) {
    int row, col;
    int pr, pc;
    int gr = (piece) ? ghost_row(board, piece) : 0;

    /* Draw each visible cell once to reduce flicker */
    for (row = 0; row < BOARD_H; row++) {
        int board_row = row + BOARD_HIDDEN;
        for (col = 0; col < BOARD_W; col++) {
            int term_x = BOARD_ORIGIN_X + t_mul(col, CELL_W);
            int term_y = BOARD_ORIGIN_Y + row;

            int draw_active = 0;
            int draw_ghost = 0;
            int active_color = 0;

            if (piece) {
                /* Check active piece cells */
                for (pr = 0; pr < PIECE_SIZE; pr++) {
                    for (pc = 0; pc < PIECE_SIZE; pc++) {
                        if (PIECES[piece->type][piece->rotation][pr][pc]) {
                            int a_row = piece->row + pr;
                            int a_col = piece->col + pc;
                            if (a_row == board_row && a_col == col) {
                                draw_active = 1;
                                active_color = PIECE_COLORS[piece->type];
                            }
                        }
                    }
                }

                /* Check ghost piece cells (only if not active here) */
                if (!draw_active) {
                    for (pr = 0; pr < PIECE_SIZE; pr++) {
                        for (pc = 0; pc < PIECE_SIZE; pc++) {
                            if (PIECES[piece->type][piece->rotation][pr][pc]) {
                                int g_row = gr + pr;
                                int g_col = piece->col + pc;
                                if (g_row == board_row && g_col == col) {
                                    draw_ghost = 1;
                                }
                            }
                        }
                    }
                }
            }

            if (draw_active) {
                render_cell(term_x, term_y, active_color);
            } else {
                int cell = board->cells[board_row][col];
                if (cell > 0) {
                    render_cell(term_x, term_y, PIECE_COLORS[cell - 1]);
                } else if (draw_ghost) {
                    screen_set_cursor(term_x, term_y);
                    screen_set_color(SCREEN_COLOR_WHITE, SCREEN_COLOR_DEFAULT);
                    screen_render_string("--");
                    screen_reset_color();
                } else {
                    render_cell(term_x, term_y, 0);
                }
            }
        }
    }
}

/*
 * render_next_piece(next_type)
 *   Draws the preview of the next piece in the HUD area.
 */
static void render_next_piece(int next_type) {
    int pr, pc;
    /* Clear 4×4 preview area first */
    for (pr = 0; pr < PIECE_SIZE; pr++) {
        screen_set_cursor(HUD_X + 1, HUD_Y + 7 + pr);
        screen_render_string("        ");   /* 8 spaces = 4 cells × 2 chars */
    }
    /* Draw the piece in rotation 0 */
    for (pr = 0; pr < PIECE_SIZE; pr++) {
        for (pc = 0; pc < PIECE_SIZE; pc++) {
            if (PIECES[next_type][0][pr][pc]) {
                screen_set_cursor(HUD_X + 1 + t_mul(pc, CELL_W), HUD_Y + 7 + pr);
                screen_set_color(PIECE_COLORS[next_type], SCREEN_COLOR_DEFAULT);
                screen_render_string("[]");
                screen_reset_color();
            }
        }
    }
}

/*
 * render_hud(state)
 *   Draws the score, high score, level, lines cleared, next piece preview,
 *   and control legend to the right of the playing field.
 *
 *   OS Module: User Interface — structured HUD panel.
 */
static void render_hud(const GameState *state) {
    char buf[20];

    /* Title */
    screen_set_color(SCREEN_COLOR_BRIGHT_CYAN, SCREEN_COLOR_DEFAULT);
    screen_set_cursor(HUD_X, HUD_Y);
    screen_render_string("=== TETRIS ===");

    /* Mode indicator (Module 6: Networking stub) */
    screen_set_color(SCREEN_COLOR_GREEN, SCREEN_COLOR_DEFAULT);
    screen_set_cursor(HUD_X, HUD_Y + 1);
    screen_render_string(" [SOLO MODE]  ");

    screen_reset_color();

    /* Score */
    screen_set_cursor(HUD_X, HUD_Y + 3);
    screen_render_string("SCORE:");
    screen_set_cursor(HUD_X, HUD_Y + 4);
    t_itoa(state->score, buf);
    screen_render_string(buf);
    screen_render_string("       ");   /* clear old digits */

    /* High score */
    screen_set_cursor(HUD_X, HUD_Y + 5);
    screen_render_string("BEST: ");
    t_itoa(state->high_score, buf);
    screen_render_string(buf);
    screen_render_string("     ");

    /* Level */
    screen_set_cursor(HUD_X, HUD_Y + 6);
    screen_render_string("LEVEL: ");
    t_itoa(state->level, buf);
    screen_render_string(buf);
    screen_render_string("  ");

    /* Next piece label */
    screen_set_cursor(HUD_X, HUD_Y + 8);
    screen_render_string("NEXT:");

    render_next_piece(state->next_type);

    /* Lines cleared */
    screen_set_cursor(HUD_X, HUD_Y + 13);
    screen_render_string("LINES: ");
    t_itoa(state->lines_cleared, buf);
    screen_render_string(buf);
    screen_render_string("  ");

    /* Controls legend */
    screen_set_color(SCREEN_COLOR_YELLOW, SCREEN_COLOR_DEFAULT);
    screen_set_cursor(HUD_X, HUD_Y + 15);
    screen_render_string("--- CONTROLS ---");
    screen_reset_color();
    screen_set_cursor(HUD_X, HUD_Y + 16);
    screen_render_string("A/D  : Move L/R ");
    screen_set_cursor(HUD_X, HUD_Y + 17);
    screen_render_string("W    : Rotate   ");
    screen_set_cursor(HUD_X, HUD_Y + 18);
    screen_render_string("S    : Soft Drop");
    screen_set_cursor(HUD_X, HUD_Y + 19);
    screen_render_string("Space: Hard Drop");
    screen_set_cursor(HUD_X, HUD_Y + 20);
    screen_render_string("Q    : Quit     ");
}

/*
 * render_game_over(state)
 *   Displays the game-over overlay and final score centered on the board.
 */
static void render_game_over(const GameState *state) {
    char buf[20];
    const int box_w = 20;
    const int box_h = 8;
    int cx = BOARD_ORIGIN_X + t_div((BOARD_W * CELL_W) - box_w, 2);
    int cy = BOARD_ORIGIN_Y + t_div(BOARD_H - box_h, 2);

    screen_set_color(SCREEN_COLOR_BRIGHT_RED, SCREEN_COLOR_DEFAULT);
    screen_set_cursor(cx, cy);
    screen_render_string("+------------------+");
    screen_set_cursor(cx, cy + 1);
    screen_render_string("|  ** GAME OVER ** |");
    screen_set_cursor(cx, cy + 2);
    screen_render_string("|                  |");
    screen_set_cursor(cx, cy + 3);
    screen_render_string("|  Score:          |");

    screen_set_cursor(cx + 10, cy + 3);
    t_itoa(state->score, buf);
    screen_render_string(buf);

    screen_set_cursor(cx, cy + 4);
    screen_render_string("|                  |");
    screen_set_cursor(cx, cy + 5);
    screen_render_string("| Press R to retry |");
    screen_set_cursor(cx, cy + 6);
    screen_render_string("| Press Q to quit  |");
    screen_set_cursor(cx, cy + 7);
    screen_render_string("+------------------+");
    screen_reset_color();
}

/* =============================================================================
 * SECTION 9: MODULE 1 — PROCESS MANAGEMENT (Main Game Loop / Scheduler)
 * ============================================================================= */

/*
 * game_reset(board, state)
 *   Clears the board and resets all game-state fields for a new game.
 *   Does NOT re-allocate board or state (they stay in virtual RAM).
 */
static void game_reset(GameBoard *board, GameState *state) {
    int r, c;
    for (r = 0; r < BOARD_TOTAL_H; r++) {
        for (c = 0; c < BOARD_W; c++) {
            board->cells[r][c] = 0;
        }
    }
    state->score           = 0;
    state->level           = 1;
    state->lines_cleared   = 0;
    state->game_over       = 0;
    state->drop_counter    = 0;
    state->drop_speed      = SPEED_INITIAL;
    state->soft_drop_active = 0;
    rand_state             = 1;
    rand_seeded            = 0;
    state->next_type       = get_random(NUM_PIECES);  /* initial preview */
}

/*
 * game_loop()
 *   The central scheduler.  Each iteration of the while loop is one "frame".
 *
 *   Frame structure (OS Module: Process Management):
 *     1. process_input  — read keyboard, dispatch actions
 *     2. process_physics — auto-drop timer, lock on collision
 *     3. process_clear   — detect and remove full lines
 *     4. process_spawn   — allocate next piece if needed
 *     5. process_render  — draw everything to the terminal
 *
 *   The busy-wait loop (delay) simulates a real timer interrupt.
 *   In a real OS this would be a hardware timer IRQ.
 */
static void game_loop(void) {
    /* ---- Allocate all game objects from virtual RAM ---------------------- */
    GameBoard *board = (GameBoard *)t_alloc((int)sizeof(GameBoard));
    GameState *state = (GameState *)t_alloc((int)sizeof(GameState));

    /* Error handling: allocation failure is fatal — cannot run */
    if (!board || !state) {
        screen_render_string("FATAL: Out of virtual memory at startup.\n");
        if (board) t_dealloc(board);
        if (state) t_dealloc(state);
        return;
    }

    /* Load high score from file system module */
    state->high_score = score_load();
    state->running    = 1;

    game_reset(board, state);

    /* Spawn the first piece */
    Piece *piece = piece_spawn(state);
    if (!piece) {
        screen_render_string("FATAL: Cannot spawn first piece.\n");
        t_dealloc(board);
        t_dealloc(state);
        return;
    }

    screen_hide_cursor();
    screen_clear();  /* Clear screen once at start */

    /* ======================================================================
     * MAIN GAME LOOP
     * Each iteration ≈ one frame.  The busy-wait at the bottom throttles FPS.
     * ====================================================================== */
    int game_over_rendered = 0;
    int frame_counter = 0;

    while (state->running) {

        /* ------------------------------------------------------------------ *
         * PROCESS 1: INPUT (OS Module: I/O Management)                       *
         * Non-blocking read so the loop never stalls waiting for a key.       *
         * ------------------------------------------------------------------ */
        frame_counter++;
        char key = keyPressed();

        if (key != '\0' && !rand_seeded) {
            seed_random(frame_counter);
            state->next_type = get_random(NUM_PIECES);
        }

        if (key != '\0') {
            if (state->game_over) {
                /* ---- Game-over input handling ---- */
                if (key == 'q' || key == 'Q') {
                    state->running = 0;
                } else if (key == 'r' || key == 'R') {
                    /* Retry: free current piece, reset, spawn fresh */
                    t_dealloc(piece);
                    game_reset(board, state);
                    piece = piece_spawn(state);
                    if (!piece) { state->running = 0; }
                }
            } else {
                /* ---- Normal gameplay input ---- */
                if      (key == 'q' || key == 'Q') state->running = 0;
                else if (key == 'a' || key == 'A') action_move_left(board, piece);
                else if (key == 'd' || key == 'D') action_move_right(board, piece);
                else if (key == 'w' || key == 'W') action_rotate(board, piece);
                else if (key == 's' || key == 'S') {
                    /* Soft drop: move piece down one row immediately */
                    if (!piece_collides(board, piece, 1, 0, piece->rotation)) {
                        piece->row++;
                        state->score += 1;
                    }
                }
                else if (key == ' ') {
                    /* Hard drop — piece freed inside action_hard_drop */
                    action_hard_drop(board, piece, state);
                    board_clear_lines(board, state);
                    piece = piece_spawn(state);
                    if (!piece || piece_collides(board, piece, 0, 0, piece->rotation)) {
                        state->game_over = 1;
                        if (piece) { t_dealloc(piece); piece = NULL; }
                    }
                    /* Reset drop counter after hard drop */
                    state->drop_counter = 0;
                }
            }
        }

        /* ------------------------------------------------------------------ *
         * PROCESS 2: PHYSICS / AUTO-DROP (OS Module: Process Management)     *
         * Gravity: piece advances one row every `drop_speed` iterations.      *
         * ------------------------------------------------------------------ */
        if (!state->game_over) {
            state->drop_counter++;

            int effective_speed = state->soft_drop_active
                                  ? t_div(state->drop_speed, 8)   /* 8× faster soft-drop */
                                  : state->drop_speed;

            if (state->drop_counter >= effective_speed) {
                state->drop_counter = 0;

                if (!piece_collides(board, piece, 1, 0, piece->rotation)) {
                    /* Clear: move piece down one row */
                    piece->row++;
                    if (state->soft_drop_active) state->score += 1;  /* soft-drop bonus */
                } else {
                    /* Collision below: lock piece, clear lines, spawn next */
                    piece_lock(board, piece, state);   /* piece freed here */
                    board_clear_lines(board, state);
                    piece = piece_spawn(state);

                    /* Error handling: if new piece immediately collides → game over */
                    if (!piece || piece_collides(board, piece, 0, 0, piece->rotation)) {
                        state->game_over = 1;
                        if (piece) { t_dealloc(piece); piece = NULL; }
                    }
                }
            }
        }

        /* ------------------------------------------------------------------ *
         * PROCESS 3: RENDER (OS Module: User Interface + I/O)                *
         * Redraw by moving cursor and overwriting (no clear = no flicker).   *
         * ------------------------------------------------------------------ */
        if (state->game_over) {
            /* Only render game over once, then just wait for input */
            if (!game_over_rendered) {
                screen_set_cursor(1, 1);
                render_border();
                render_board(board, NULL);
                render_hud(state);
                render_game_over(state);
                fflush(stdout);
                game_over_rendered = 1;
            }
        } else {
            game_over_rendered = 0;
            screen_set_cursor(1, 1);
            render_border();
            render_board(board, piece);
            render_hud(state);
            fflush(stdout);
        }

        /* Move cursor to safe position to avoid artifacts */
        screen_set_cursor(1, BOARD_ORIGIN_Y + BOARD_H + 2);

        /* ------------------------------------------------------------------ *
         * Frame throttle via busy-wait.                                      *
         * usleep() / nanosleep() require <unistd.h> / <time.h> which are     *
         * outside the allowed library list (Rule 3).  Busy-wait over a       *
         * volatile counter is the spec-compliant alternative; the volatile.  *
         * keyword prevents the compiler from optimising the loop away.       *
         * ------------------------------------------------------------------ */
        volatile int delay_i = 0;
        while (delay_i < 50000) { delay_i++; }
    }

    /* ======================================================================
     * SHUTDOWN — OS Module: Memory Management cleanup
     * All allocations from virtual RAM must be freed before memory_cleanup().
     * ====================================================================== */
    if (piece) {
        t_dealloc(piece);      /* free any in-flight piece */
        piece = NULL;
    }

    /* Persist high score to the file system module */
    score_save(state->high_score);

    t_dealloc(board);
    t_dealloc(state);

    screen_show_cursor();
}

/* =============================================================================
 * SECTION 10: ENTRY POINT
 * ============================================================================= */

/*
 * main()
 *   Boot sequence mirrors a real OS:
 *     1. Memory subsystem init (virtual RAM slab allocation)
 *     2. I/O subsystem init    (keyboard raw mode)
 *     3. Run the game          (process scheduler / game loop)
 *     4. I/O teardown          (restore terminal)
 *     5. Memory teardown       (free virtual RAM slab)
 */
int main(void) {
    /* ---- 1. Boot memory subsystem ---------------------------------------- */
    memory_init(VIRTUAL_RAM_SIZE);

    /* ---- 2. Boot I/O subsystem ------------------------------------------- */
    keyboard_init();

    /* ---- 3. Run game loop (all game logic lives here) -------------------- */
    game_loop();

    /* ---- 4. Shutdown message --------------------------------------------- */
    screen_clear();
    screen_set_cursor(1, 1);
    screen_render_string("Thanks for playing Tetris OS! Score saved.\n");

    /* ---- 5. Teardown in reverse boot order -------------------------------- */
    keyboard_restore();
    memory_cleanup();

    return 0;
}
