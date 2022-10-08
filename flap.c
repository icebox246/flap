#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define ESC "\x1B"

#define FLOOR_Y 40
#define WIDTH 64

void init_terminal(void) {
    struct termios attrs;
    tcgetattr(1, &attrs);
    attrs.c_lflag &= ~ICANON;
    attrs.c_lflag &= ~ECHO;
    tcsetattr(1, TCSANOW, &attrs);

    int flags = fcntl(1, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(1, F_SETFL, flags);
}

void clear_screen(void) { printf(ESC "[2J"); }

void put_text(int x, int y, char* str) { printf(ESC "[%d;%dH%s", y, x, str); }

void put_rect(int x, int y, int w, int h, int fill) {
    if (x > WIDTH) return;
    if (x + w - 1 > WIDTH) w = WIDTH - x;
    if (x < 1) {
        w += x - 1;
        x = 1;
    }
    if (w <= 0) return;
    for (int i = y; i < y + h; i++) {
        if (y < 0) continue;
        printf(ESC "[%d;%dH", i, x);
        for (int j = 0; j < w; j++) {
            putc(fill, stdout);
        }
    }
}

void move_cursor(int x, int y) { printf(ESC "[%d;%dH", y, x); }

#define C_BLACK 30
#define C_RED 31
#define C_GREEN 32
#define C_YELLOW 33
#define C_BLUE 34
#define C_MAGENTA 35
#define C_CYAN 36
#define C_WHITE 37
#define C_DEFAULT 39

void set_color(int fg, int bg) { printf(ESC "[%d;%dm", fg, bg + 10); }

double get_fsecs() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_nsec * 1e-9 + ts.tv_sec;
}

#define GRAVITY 50
#define JUMP_VEL 20

typedef struct {
    double x, y;
    double ly;
    double vy;
    int dead;
} Player;

#define PIPE_GAP 8
#define PIPE_SPACING 30
#define PIPE_START_SPEED 15
#define PIPE_ACCELERATION 0.5
#define PIPE_MAX_SPEED 35
#define PIPE_WIDTH 5
#define PIPE_COUNT 8
#define PIPE_SPREAD 8

typedef struct {
    double x, lx;
    int y;
    int scored;
} Pipe;

typedef struct {
    Player player;
    Pipe pipes[PIPE_COUNT];
    int score;
    double pipe_speed;
} State;

void draw_player(Player* p) {
    if (!p->dead)
        set_color(C_YELLOW, C_DEFAULT);
    else
        set_color(C_RED, C_DEFAULT);
    put_text(p->x, p->ly, " ");
    put_text(p->x, p->y, "@");
}

void update_player(Player* p, double dt, int should_jump) {
    p->ly = p->y;
    if (!p->dead && should_jump) {
        p->vy = -JUMP_VEL;
    } else {
        p->vy += GRAVITY * dt;
    }
    p->y += p->vy * dt;

    if (p->y >= FLOOR_Y) {
        p->dead = 1;
        p->y = FLOOR_Y;
        p->vy = 0;
    }
}

void update_pipes(Pipe* pipes, float pipe_speed, float dt) {
    for (int i = 0; i < PIPE_COUNT; i++) {
        Pipe* p = &pipes[i];
        p->lx = p->x;
        p->x -= pipe_speed * dt;

        if (p->x < -PIPE_WIDTH) {
            double nx = 0;
            double ny = 0;
            for (int j = 0; j < PIPE_COUNT; j++) {
                if (pipes[j].x > nx) {
                    nx = pipes[j].x;
                    ny = pipes[j].y;
                }
            }
            nx += PIPE_WIDTH + PIPE_SPACING;
            ny += rand() % (PIPE_SPREAD * 2) - PIPE_SPREAD;
            if (ny >= FLOOR_Y) ny = FLOOR_Y - 1;
            if (ny <= PIPE_GAP) ny = PIPE_GAP + 1;
            p->x = nx;
            p->y = ny;
            p->lx = p->x;
            p->scored = 0;
        }
    }
}

void draw_single_pipe(Pipe* p) {
    set_color(C_DEFAULT, C_DEFAULT);
    put_rect(p->lx, p->y, PIPE_WIDTH, FLOOR_Y - (int)p->y, ' ');
    put_rect(p->lx, 1, PIPE_WIDTH, (int)p->y - PIPE_GAP, ' ');

    set_color(C_BLUE, C_DEFAULT);

    put_rect(p->x, p->y, 1, FLOOR_Y - (int)p->y, '|');
    put_rect(p->x + 1, p->y, PIPE_WIDTH - 2, FLOOR_Y - (int)p->y, '#');
    put_rect(p->x + PIPE_WIDTH - 1, p->y, 1, FLOOR_Y - (int)p->y, '|');

    put_rect(p->x, 1, 1, (int)p->y - PIPE_GAP - 1, '|');
    put_rect(p->x + 1, 1, PIPE_WIDTH - 2, (int)p->y - PIPE_GAP - 1, '#');
    put_rect(p->x + PIPE_WIDTH - 1, 1, 1, (int)p->y - PIPE_GAP - 1, '|');

    set_color(C_CYAN, C_DEFAULT);

    put_rect(p->x, p->y, PIPE_WIDTH, 1, '=');
    put_rect(p->x, p->y - PIPE_GAP, PIPE_WIDTH, 1, '=');
}

void draw_pipes(Pipe* pipes) {
    for (int i = 0; i < PIPE_COUNT; i++) {
        draw_single_pipe(pipes + i);
    }
}

void collide_player_with_pipes(Player* player, Pipe* pipes, int* score) {
    for (int i = 0; i < PIPE_COUNT; i++) {
        Pipe* pipe = &pipes[i];
        if (player->x >= pipe->x && player->x < pipe->x + PIPE_WIDTH) {
            if (player->y >= pipe->y || player->y <= pipe->y - PIPE_GAP + 1) {
                player->dead = 1;
            } else if (!pipe->scored) {
                pipe->scored = 1;
                (*score)++;
            }
            return;
        }
    }
}

void draw_floor(void) {
    set_color(C_GREEN, C_DEFAULT);
    put_rect(0, FLOOR_Y, WIDTH, 1, '^');
}

void init_state(State* st) {
    memset(st, 0, sizeof(State));
    st->player.x = 10;
    st->player.y = FLOOR_Y * 0.5f;

    for (int i = 0; i < PIPE_COUNT; i++) {
        st->pipes[i].x = -PIPE_WIDTH - 1;
    }
    st->pipes[0].x = WIDTH;
    st->pipes[0].y = (FLOOR_Y + PIPE_GAP) >> 1;

    st->pipe_speed = PIPE_START_SPEED;
}

void update_pipe_speed(double* pipe_speed, double dt) {
    *pipe_speed += dt * PIPE_ACCELERATION;
    if (*pipe_speed > PIPE_MAX_SPEED) *pipe_speed = PIPE_MAX_SPEED;
}

int main(void) {
    init_terminal();

    double last_time, now, dt;
    last_time = now = get_fsecs();

    clear_screen();

    srand(time(0));
    State st;
    init_state(&st);

    while (1) {
        now = get_fsecs();
        dt = now - last_time;
        last_time = now;

        int should_jump = 0;
        int should_restart = 0;

        // input
        int c;
        while ((c = getc(stdin)) != EOF) {
            if (c == ' ') {
                should_jump = 1;
            }
            if (st.player.dead && c == 'r') {
                should_restart = 1;
            }
        }

        // restarting
        if (should_restart) {
            srand(time(0));
            init_state(&st);
            set_color(C_DEFAULT, C_DEFAULT);
            clear_screen();
            continue;
        }

        // updating
        update_pipes(st.pipes, st.pipe_speed, dt);
        update_player(&st.player, dt, should_jump);
        collide_player_with_pipes(&st.player, st.pipes, &st.score);
        update_pipe_speed(&st.pipe_speed, dt);

        // drawing
        draw_floor();
        draw_pipes(st.pipes);
        draw_player(&st.player);

        char buffer[64] = {0};
        sprintf(buffer, " score: %3d | speed: %3.0f ", st.score, st.pipe_speed);
        set_color(C_YELLOW, C_DEFAULT);
        put_rect(1, FLOOR_Y + 1, WIDTH, 1, '=');
        set_color(C_DEFAULT, C_DEFAULT);
        put_text(3, FLOOR_Y + 1, buffer);

        if (st.player.dead) {
            set_color(C_BLACK, C_RED);
            put_text(15, 15, "press 'r' to restart.");
        }

        move_cursor(1, FLOOR_Y + 1);
        fflush(stdout);

        usleep(1e3);
    }
}
