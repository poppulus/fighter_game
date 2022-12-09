#include "SDL_FontCache.h"

#define W_WIDTH     640
#define W_HEIGHT    480
#define FPS_CAP_MS  16.66666603F

#define P_XVEL              250
#define P_XVEL_JUMP         200
#define P_XVEL_BW           175
#define P_XVEL_DOUBLE       500
#define P_XVEL_COL          125
#define P_XVEL_STEER        150
#define P_DASHVEL           400
#define P_DASHVEL_BW        300
#define P_YVEL              900
#define P_JUMPVEL          -500
#define P_JUMPVEL_D        -350
#define P_KNOCKDOWN_YVEL    450

#define PLAYER_ONE 0
#define PLAYER_TWO 1

#define OPTIONS_COUNT 5

typedef enum 
{
    G_STATE_START,
    G_STATE_MENU,
    G_STATE_PLAY,
    G_STATE_QUIT
} Game_State;

typedef enum
{
    G_OPTION_E_STATE,
    G_OPTION_E_BLOCK,
    G_OPTION_P_KICK_START,
    G_OPTION_P_KICK_ACTIVE,
    G_OPTION_P_KICK_RECOVERY,
} Game_Option;

typedef enum
{
    OPTION_STATE_STAND,
    OPTION_STATE_CROUCH,
    OPTION_STATE_JUMP
} Option_State;

typedef enum 
{
    P_METHOD_KEYBOARD,
    P_METHOD_GAMEPAD,
    P_METHOD_DUMMY
} Player_Method;

typedef enum
{
    P_INPUT_LEFT = 1,
    P_INPUT_UP = 2,
    P_INPUT_RIGHT = 4,
    P_INPUT_DOWN = 8,
    P_INPUT_PUNCH = 16,
    P_INPUT_KICK = 32
} Player_Input;

typedef enum 
{
    P_STATE_STAND,
    P_STATE_CROUCH,
    P_STATE_JUMP,
    P_STATE_FALL,
    P_STATE_KNOCKDOWN,
    P_STATE_BLOCK,
    P_STATE_BLOCK_LOW,
    P_STATE_BLOCK_AIR,
    P_STATE_DASH_G,
    P_STATE_DASH_G_BW,
    P_STATE_DASH_A,
    P_STATE_DASH_A_BW,
    P_STATE_PUNCH,
    P_STATE_HIT_PUNCH,
    P_STATE_KICK,
    P_STATE_HIT_KICK,
    P_STATE_KICK_LOW,
    P_STATE_HIT_KICK_LOW,
    P_STATE_KICK_AIR
} Player_State;

typedef enum 
{
    P_FACE_LEFT,
    P_FACE_RIGHT
} Player_Facing;
/*
typedef enum
{
    P_MOVE_PUNCH,
    P_MOVE_PUNCH_LOW,
    P_MOVE_KICK,
    P_MOVE_KICK_LOW,
} Player_Move;
*/
typedef enum 
{
    P_ATTACK_STARTUP,
    P_ATTACK_ACTIVE,
    P_ATTACK_RECOVERY,
    P_ATTACK_HIT
} Player_Attack_State;


struct Player_Move
{
    int startup,
        active,
        recovery;
};

typedef struct Player_Move Move_Punch;
typedef struct Player_Move Move_Kick;

typedef struct
{
    Move_Punch punch;
    Move_Kick kick;
} Move_List;

typedef struct Player
{
    SDL_Rect r, col, hbox_mid, hbox_high, hbox_low, a_hbox;

    Player_Method input_method;

    Move_List *m_list;

    unsigned char   input, last_input, 
                    corner:1, double_jump:1, block:1, 
                    dash:1, freeze:1, attack:1, attack_air:1, hit:1, move_hit:1;

    char    state, last_state, attack_state, facing, xdir, dash_dir, i_queue[8];
    int     xvel, freeze_timer, attack_timer, hit_timer;
    float   x, y, yvel;

    Uint64  dash_begin, dash_end, dash_counter, 
            freeze_begin, freeze_end,
            attack_recovery, attack_startup, attack_active,
            knockdown_begin;
} Player;

typedef struct Game
{
    Move_List move_list;
    Player *players;
    unsigned char state, opt_select, opt_state, game_freeze:1, quit:1;
    int g_hit_timer;
} Game;


int find_frame_data(char *buf, const char *s)
{
    if (strcmp(buf, s) == 0) return 1;
    return 0;
}

void read_from_file(Game *g)
{
    FILE *f;
    char buf[64];
    int c, i = 0, n = 0, *l = NULL;

    if (!(f = fopen("config.cfg", "a+"))) 
    {
        printf("could not open/append to file: %s\n", "config.cfg");
        return;
    }

    memset(buf, 0, 64);

    while (!feof(f))
    {
        c = fgetc(f);

        if (c != '\n')
        {
            buf[i] = c;
            if (++i > 63) i = 0;
        }
        else
        {
            l = NULL;
            n = 0;
            i = 0;
            memset(buf, 0, 64);
            continue;
        }

        if (c == ' ')
        {
            if (n == 0)
            {
                if (find_frame_data(buf, "punch_startup_frames "))
                    l = &g->move_list.punch.startup;

                else if (find_frame_data(buf, "punch_active_frames "))
                    l = &g->move_list.punch.active;

                else if (find_frame_data(buf, "punch_recovery_frames "))
                    l = &g->move_list.punch.recovery;

                else if (find_frame_data(buf, "kick_startup_frames "))
                    l = &g->move_list.kick.startup;

                else if (find_frame_data(buf, "kick_active_frames "))
                    l = &g->move_list.kick.active;

                else if (find_frame_data(buf, "kick_recovery_frames "))
                    l = &g->move_list.kick.recovery;
            }
        }
        
        if (c == '"')
        {
            if (l)
            {
                n++;
                if (n == 2)
                {
                    int a = 0;
                    for (int j = sizeof(buf) - 1; j >= 0; j--)
                    {
                        if (buf[j] == '"') a++;
                        if (a == 2)
                        {
                            *l = atoi(&buf[j + 1]);
                            break;
                        }
                    }
                    l = NULL;
                    n = 0;
                    i = 0;
                    memset(buf, 0, 64);
                }
            }
        }
    }

    fclose(f);
}

void save_to_file(Game *g)
{
    FILE *f;
    //int c, i = 0, n = 0, *l = NULL;

    if (!(f = fopen("config.cfg", "r+"))) 
    {
        printf("could not open/append to file: %s\n", "config.cfg");
        return;
    }

    int *l = NULL;
    char buf[128];
    memset(buf, 0, 128);

    while (fscanf(f, "%s ", buf) == 1)
    {
        printf("%s\n", buf);
        if (find_frame_data(buf, "kick_startup_frames"))
        {
            l = &g->move_list.kick.startup;
            puts("found kick startup");
            char b[2];
            SDL_itoa(*l, b, 10);
            fprintf(f, "%c%s%c%c", '"', b, '"', '\n');
        }
    }

    fclose(f);
}

void queue_put(char *queue, char val)
{
    for (int i = 7; i > 0; i--)
        queue[i] = queue[i - 1];
    
    queue[0] = val;
}

void queue_remove(char *queue, char val)
{
    for (int i = 0; i < 8; i++)
    {
        if (queue[i] == val)
        {
            for (int j = i; j < 7; j++)
                queue[j] = queue[j + 1];

            break;
        }
    }
}

void g_init(Game *g)
{
    g->move_list.punch.startup = 0;
    g->move_list.punch.active = 0;
    g->move_list.punch.recovery = 0;

    g->move_list.kick.startup = 0;
    g->move_list.kick.active = 0;
    g->move_list.kick.recovery = 0;

    read_from_file(g);

    g->players = NULL;
    g->opt_select = G_OPTION_E_STATE;
    g->opt_state = OPTION_STATE_STAND;
    g->game_freeze = 0;
    g->quit = 0;
    g->g_hit_timer = 0;
    g->state = G_STATE_START;
}

void g_close(Game *g)
{
    if (g->players != NULL)
        free(g->players);
}

void g_open_controllers(SDL_GameController **c, int n)
{
    for (int i = 0; i < n; i++)
    {
        if ((c[i] = SDL_GameControllerOpen(i)) == NULL)
        {
            printf("Could not open gamepad: %d, %s\n", i, SDL_GetError());
        }
    }
}

void g_close_controllers(SDL_GameController **c, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (c[i] != NULL) 
            SDL_GameControllerClose(c[i]);
    }
}

void g_render_option_state(SDL_Renderer *r, FC_Font *f, SDL_Rect q, int state)
{
    switch (state)
    {
        default: break;
        case OPTION_STATE_STAND:
            FC_Draw(f, r, q.w - 20, q.y + 60, "Stand");
            break;
        case OPTION_STATE_CROUCH:
            FC_Draw(f, r, q.w - 20, q.y + 60, "Crouch");
            break;
        case OPTION_STATE_JUMP:
            FC_Draw(f, r, q.w - 20, q.y + 60, "Jump");
            break;
    }
}
void g_render_option_block(SDL_Renderer *r, FC_Font *f, SDL_Rect q, int block)
{
    if (block) FC_Draw(f, r, q.w - 20, q.y + 80, "Block");
    else FC_Draw(f, r, q.w - 20, q.y + 80, "Don't Block");
}
void g_render_option_kick_startup(SDL_Renderer *r, FC_Font *f, SDL_Rect q, int val)
{
    char str[16];
    if (SDL_itoa(val, str, 10) != NULL)
    {
        FC_Draw(f, r, q.w - 20, q.y + q.h - 80, str);
    }
}
void g_render_option_kick_active(SDL_Renderer *r, FC_Font *f, SDL_Rect q, int val)
{
    char str[16];
    if (SDL_itoa(val, str, 10) != NULL)
    {
        FC_Draw(f, r, q.w - 20, q.y + q.h - 60, str);
    }
}
void g_render_option_kick_recovery(SDL_Renderer *r, FC_Font *f, SDL_Rect q, int val)
{
    char str[16];
    if (SDL_itoa(val, str, 10) != NULL)
    {
        FC_Draw(f, r, q.w - 20, q.y + q.h - 40, str);
    }
}

void p_render(SDL_Renderer *r, Player p)
{
    // draw player 
    if (p.facing == P_FACE_RIGHT) 
        SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
    else
        SDL_SetRenderDrawColor(r, 0, 0, 255, 255);

    SDL_RenderFillRect(r, &p.r);

    if (p.hit)
    {
        SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
        SDL_RenderDrawRect(r, &p.r);
    }

    // draw hitboxes
    SDL_SetRenderDrawColor(r, 0, 255, 0, 128);
    SDL_RenderFillRect(r, &p.col);

    SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
    SDL_RenderDrawRect(r, &p.hbox_high);
    SDL_RenderDrawRect(r, &p.hbox_mid);
    SDL_RenderDrawRect(r, &p.hbox_low);

    SDL_SetRenderDrawColor(r, 255, 255, 0, 128);
    SDL_RenderFillRect(r, &p.hbox_high);
    SDL_RenderFillRect(r, &p.hbox_mid);
    SDL_RenderFillRect(r, &p.hbox_low);

    if (p.state == P_STATE_KICK 
    || p.state == P_STATE_KICK_LOW 
    || p.state == P_STATE_KICK_AIR)
    {
        if (p.attack_state == P_ATTACK_ACTIVE)
        {
            SDL_SetRenderDrawColor(r, 0, 255, 0, 128);
            SDL_RenderFillRect(r, &p.a_hbox);
        }
    }

    if (p.block)
    {
        SDL_SetRenderDrawColor(r, 0, 255, 255, 128);
        SDL_RenderDrawRect(r, &p.r);

        if (p.hit) SDL_RenderFillRect(r, &p.r);
    }
}

int p_coll(SDL_Rect a, SDL_Rect b)
{
    int left_a = a.x,
        right_a = a.x + a.w,
        top_a = a.y,
        bottom_a = a.y + a.h;

    int left_b = b.x,
        right_b = b.x + b.w,
        top_b = b.y,
        bottom_b = b.y + b.h;

    if (left_a >= right_b) return 0;
    if (right_a <= left_b) return 0;
    if (top_a >= bottom_b) return 0;
    if (bottom_a <= top_b) return 0;

    return 1;
}

void p_dash_cancel(Player *p)
{
    p->dash = 0;
    p->dash_dir = 0;
    p->dash_begin = 0;
    p->dash_counter = 0;
    p->dash_end = 0;
}

void p_freeze_reset(Player *p)
{
    p->freeze = 0;
    p->freeze_timer = 0;
    p->freeze_begin = 0;
    p->freeze_end = 0;
}

void p_attack_reset(Player *p)
{
    p->attack_state = P_ATTACK_STARTUP;
    p->attack_timer = 0;
    p->attack = 0;
    p->attack_active = 0;
    p->attack_recovery = 0;
    p->attack_startup = 0;
}

void p_init(Player *p, Move_List *moves, int xpos)
{
    p->m_list = moves;

    for (int i = 0; i < 8; i++)
        p->i_queue[i] = 0;

    p->input_method = 0;
    p->input = 0;
    p->last_input = 0;

    p->facing = 0;

    p->corner = 0;
    p->double_jump = 0;
    p->hit = 0;
    p->move_hit = 0;
    p->block = 0;
    p->attack_air = 0;

    p->x = xpos;
    p->y = W_HEIGHT - 80;

    p->r.x = (int)p->x;
    p->r.y = (int)p->y;
    p->r.w = 40;
    p->r.h = 80;

    p->col.w = 1;
    p->col.h = 80;
    p->col.x = p->r.x + (p->r.w >> 1);
    p->col.y = p->r.y;

    p->a_hbox.w = 60;
    p->a_hbox.h = 20;

    p->hbox_high.w = 20;
    p->hbox_high.h = 20;

    p->hbox_mid.w = 20;
    p->hbox_mid.h = 20;

    p->hbox_low.w = 20;
    p->hbox_low.h = 20;

    p->xdir = 0;
    p->xvel = P_XVEL;
    p->yvel = 0;

    p->hit_timer = 0;
    p->knockdown_begin = 0;

    p_freeze_reset(p);
    p_dash_cancel(p);
    p_attack_reset(p);

    p->last_state = P_STATE_STAND;
    p->state = P_STATE_STAND;
}

void p_reset_pos(Player *p, int xpos)
{
    p->last_input = 0;

    p->corner = 0;
    p->double_jump = 0;
    p->hit = 0;
    p->move_hit = 0;
    p->attack_air = 0;

    p->xdir = 0;
    p->xvel = P_XVEL;
    p->yvel = 0;

    p->r.h = 80;
    p->col.h = 80;

    p->x = xpos;
    p->y = W_HEIGHT - 80;

    p->hit_timer = 0;
    p->knockdown_begin = 0;

    p_freeze_reset(p);
    p_dash_cancel(p);
    p_attack_reset(p);

    p->last_state = P_STATE_STAND;
    p->state = P_STATE_STAND;
}

int p_same_height(SDL_Rect a, SDL_Rect b)
{
    if ((a.y >= b.y && a.y <= b.y + b.h)
    || (a.y + a.h >= b.y && a.y + a.h <= b.y + b.h)) 
        return 1;

    return 0;
}

void p_side_adjust(Player *p, SDL_Rect opp_rect)
{
    if (p->col.x < opp_rect.x)
    {
        p->facing = P_FACE_RIGHT;
    }
    else if (p->col.x > opp_rect.x)
    {
        p->facing = P_FACE_LEFT;
    }
}

void p_update_state_stand(Player *p, float delta)
{
    if (p->input & P_INPUT_DOWN)
    {
        p->r.h = 60;
        p->col.h = 60;
        p->xdir = 0;
        p->state = P_STATE_CROUCH;
        p->last_state = P_STATE_STAND;
        return;
    }

    if (p->input & P_INPUT_LEFT) p->xdir = -1;
    else if (p->input & P_INPUT_RIGHT) p->xdir = 1;
    else p->xdir = 0;

    if (p->facing == P_FACE_RIGHT)
    {
        if (p->xdir < 0)
            p->x += p->xdir * P_XVEL_BW * delta;
        else if (p->xdir > 0)
            p->x += p->xdir * P_XVEL * delta;
    }
    else
    {
        if (p->xdir < 0)
            p->x += p->xdir * P_XVEL * delta;
        else if (p->xdir > 0)
            p->x += p->xdir * P_XVEL_BW * delta;
    }

    if (p->input & P_INPUT_UP)
    {
        p->xvel = P_XVEL_JUMP;
        p->yvel = P_JUMPVEL;
        p->state = P_STATE_JUMP;
        p->last_state = P_STATE_STAND;
    }
}

void p_update_state_crouch(Player *p, float delta)
{
    if (!(p->input & P_INPUT_DOWN))
    {
        p->r.h = 80;
        p->col.h = 80;
        p->state = P_STATE_STAND;
        p->last_state = P_STATE_CROUCH;
    }
}

void p_update_state_jump(Player *p, float delta)
{
    p->x += p->xdir * p->xvel * delta;

    if ((p->yvel += P_YVEL * delta) >= 0) 
    {
        p->state = P_STATE_FALL;
        p->last_state = P_STATE_JUMP;
    }

    p->y += p->yvel * delta;
}

void p_update_state_fall(Player *p, float delta)
{
    p->x += p->xdir * p->xvel * delta;
    p->yvel += P_YVEL * delta;
    p->y += p->yvel * delta;

    if (p->y + p->r.h >= W_HEIGHT)
    {
        p->attack_air = 0;
        p->double_jump = 0;
        p->xvel = P_XVEL;
        p->yvel = 0;
        p->y = W_HEIGHT - p->r.h;
        p_dash_cancel(p);
        p->state = P_STATE_STAND;
        p->last_state = P_STATE_FALL;
    }
}

void p_update_state_knockdown(Player *p, float delta)
{
    if (p->y + p->r.h >= W_HEIGHT)
    {
        p->y = W_HEIGHT - p->r.h;
    }
    else
    {
        p->y += p->yvel * delta;
        p->yvel += P_KNOCKDOWN_YVEL * delta;
    }

    if ((SDL_GetTicks64() - p->knockdown_begin) >= 1000) 
    {
        p->hit = 0;
        p->hit_timer = 0;
        p->knockdown_begin = 0;
        p->r.h = 80;
        p->col.h = 80;
        p->y = W_HEIGHT - 80;
        p->state = P_STATE_STAND;
        p->last_state = P_STATE_STAND;
    }
}

void p_update_state_block(Player *p, int recovery, float delta)
{
    p->x += p->xdir;
    if (++p->hit_timer >= recovery)
    {
        p->hit_timer = 0;
        p->hit = 0;
        p->block = 0;
        p->state = P_STATE_STAND;
        p->last_state = P_STATE_STAND;
    }
}

void p_update_state_block_low(Player *p, int recovery, float delta)
{
    p->x += p->xdir;
    if (++p->hit_timer >= recovery)
    {
        p->hit_timer = 0;
        p->hit = 0;
        p->block = 0;
        p->state = P_STATE_CROUCH;
        p->last_state = P_STATE_CROUCH;
    }
}

void p_update_state_block_air(Player *p, int recovery, float delta)
{

}

void p_update_state_dash_g(Player *p, float delta)
{
    p->x += p->xdir * P_DASHVEL * delta;

    if ((p->facing == P_FACE_RIGHT && p->input & P_INPUT_LEFT) 
    || (p->facing == P_FACE_LEFT && p->input & P_INPUT_RIGHT))
    {
        p_dash_cancel(p);
        p->state = P_STATE_STAND;
        p->last_state = P_STATE_STAND;
        return;
    }

    if ((SDL_GetTicks64() - p->dash_begin) >= 250) 
    {
        p_dash_cancel(p);
        p->xvel = P_XVEL;
        p->state = P_STATE_STAND;
        p->last_state = P_STATE_STAND;
    }
}

void p_update_state_dash_g_bw(Player *p, float delta)
{
    p->x += p->xdir * P_DASHVEL * delta;

    if ((SDL_GetTicks64() - p->dash_begin) >= 150) 
    {
        p_dash_cancel(p);
        p->xvel = P_XVEL;
        p->state = P_STATE_STAND;
        p->last_state = P_STATE_STAND;
    }
}

void p_update_state_dash_a(Player *p, float delta)
{
    if (p->freeze)
    {
        if ((SDL_GetTicks64() - p->freeze_begin) >= (FPS_CAP_MS * 4))
        {
            p->freeze = 0; 
            p->freeze_begin = 0;
            p->dash_begin = SDL_GetTicks64();
        }
        else return;
    }

    p->x += p->xdir * P_DASHVEL * delta;

    if ((SDL_GetTicks64() - p->dash_begin) >= 150) 
    {
        p->yvel = 0;
        p->state = P_STATE_FALL;
        p->last_state = P_STATE_FALL;
    }
}

void p_update_state_dash_a_bw(Player *p, float delta)
{
    if (p->freeze) 
    {
        if ((SDL_GetTicks64() - p->freeze_begin) >= (FPS_CAP_MS * 2))
        {
            p->freeze = 0; 
            p->freeze_begin = 0;
            p->dash_begin = SDL_GetTicks64();
        }
        else return;
    }

    p->x += p->xdir * P_DASHVEL * delta;
    
    if ((SDL_GetTicks64() - p->dash_begin) >= 100) 
    {
        p->yvel = 0;
        p->state = P_STATE_FALL;
        p->last_state = P_STATE_FALL;
    }
}

void p_update_punch(Player *p, float delta)
{
    
}

void p_update_hit_punch(Player *p, int face, float delta)
{

}

void p_update_kick(Player *p, int opp_corner, float delta)
{
    ++p->attack_timer;

    switch (p->attack_state)
    {
        default: break;
        case P_ATTACK_STARTUP:
            if (p->attack_timer == p->m_list->kick.startup)
            {
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_ACTIVE;
            }
            break;
        case P_ATTACK_ACTIVE:
            if (p->move_hit)
            {
                if (opp_corner) 
                {
                    p->x += p->facing ? -20 : 20;
                }
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_HIT;
                break;
            }

            if (p->attack_timer == p->m_list->kick.active)
            {
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_RECOVERY;
            }
            break;
        case P_ATTACK_RECOVERY:
            if (p->attack_timer == p->m_list->kick.recovery)
            {
                p_attack_reset(p);
                p->state = P_STATE_STAND;
            }
            break;
        case P_ATTACK_HIT:
            if (p->attack_timer == 24)
            {
                p->move_hit = 0;
                p_attack_reset(p);
                p->state = P_STATE_STAND;
            }
            break;
    }

    if (p->facing == P_FACE_RIGHT)
        p->a_hbox.x = p->r.x + p->r.w;
    else
        p->a_hbox.x = p->r.x - p->a_hbox.w;

    p->a_hbox.y = p->r.y + (p->r.h >> 1) - (p->a_hbox.h >> 1);
}

void p_update_hit_kick(Player *p, float delta)
{
    p->x += p->xdir * 80 * delta;

    if (++p->hit_timer >= 20)
    {
        p->hit = 0;
        p->hit_timer = 0;

        switch (p->last_state)
        {
            default: break;
            case P_STATE_STAND:
            case P_STATE_DASH_G:
            case P_STATE_DASH_G_BW:
            case P_STATE_HIT_KICK:
            case P_STATE_HIT_KICK_LOW:
                p->yvel = 0;
                p->state = P_STATE_STAND;
                p->last_state = P_STATE_STAND;
                break;
            case P_STATE_JUMP:
            case P_STATE_FALL:
            case P_STATE_DASH_A:
            case P_STATE_DASH_A_BW:
                p->yvel = 0;
                p->state = P_STATE_FALL;
                p->last_state = P_STATE_FALL;
                break;
        }
    }
}

void p_update_kick_low(Player *p, int opp_hit, int opp_corner, float delta)
{
    ++p->attack_timer;

    switch (p->attack_state)
    {
        default: break;
        case P_ATTACK_STARTUP:
            if (p->attack_timer == p->m_list->kick.startup)
            {
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_ACTIVE;
            }
            break;
        case P_ATTACK_ACTIVE:
            if (p->move_hit)
            {
                if (opp_corner) 
                {
                    p->x += p->facing ? -20 : 20;
                }
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_HIT;
                break;
            }

            if (p->attack_timer == p->m_list->kick.active)
            {
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_RECOVERY;
            }
            break;
        case P_ATTACK_RECOVERY:
            if (p->attack_timer == p->m_list->kick.recovery)
            {
                p_attack_reset(p);
                if (p->input & P_INPUT_DOWN)
                {
                    p->r.h = 60;
                    p->col.h = 60;
                    p->state = P_STATE_CROUCH;
                }
                else
                {
                    p->r.h = 80;
                    p->col.h = 80;
                    p->state = P_STATE_STAND;
                }
            }
            break;
        case P_ATTACK_HIT:
            if (p->attack_timer == 24)
            {
                p->move_hit = 0;
                p_attack_reset(p);
                if (p->input & P_INPUT_DOWN)
                {
                    p->r.h = 60;
                    p->col.h = 60;
                    p->state = P_STATE_CROUCH;
                }
                else
                {
                    p->r.h = 80;
                    p->col.h = 80;
                    p->state = P_STATE_STAND;
                }
            }
            break;
    }

    if (p->facing == P_FACE_RIGHT)
        p->a_hbox.x = p->r.x + p->r.w;
    else
        p->a_hbox.x = p->r.x - p->a_hbox.w;

    p->a_hbox.y = p->r.y + p->r.h - p->a_hbox.h;
}

void p_update_hit_kick_low(Player *p, float delta)
{
    if (p->block)
    {
        p->block = 0;
        p->state = P_STATE_STAND;
    }
    else
    {
        if (p->facing == P_FACE_RIGHT)
        {
            if (p->input & P_INPUT_LEFT)
            {
                p->block = 1;
            }
            else
            {
                p->knockdown_begin = SDL_GetTicks64();
                p->r.h = 40;
                p->yvel = -1;
                p->state = P_STATE_KNOCKDOWN;
            }
        }
        else
        {
            if (p->input & P_INPUT_RIGHT)
            {
                p->block = 1;
            }
            else
            {
                p->knockdown_begin = SDL_GetTicks64();
                p->r.h = 40;
                p->yvel = -1;
                p->state = P_STATE_KNOCKDOWN;
            }
        }
    }
}

void p_update_kick_air(Player *p, int opp_hit, int opp_corner, float delta)
{
    ++p->attack_timer;

    switch (p->attack_state)
    {
        default: break;
        case P_ATTACK_STARTUP:
            if (p->attack_timer == 8)
            {
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_ACTIVE;
            }
            break;
        case P_ATTACK_ACTIVE:
            if (p->move_hit)
            {
                if (opp_corner) 
                {
                    p->x += p->facing ? -10 : 10;
                }
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_HIT;
                break;
            }

            if (p->attack_timer == 4)
            {
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_RECOVERY;
            }
            break;
        case P_ATTACK_RECOVERY:
            if (p->attack_timer == 12)
            {
                p_attack_reset(p);
                if (p->last_state == P_STATE_JUMP)
                    p->state = P_STATE_JUMP;
                else if (p->last_state == P_STATE_FALL)
                    p->state = P_STATE_FALL;
                else 
                    p->state = P_STATE_FALL;
            }
            break;
        case P_ATTACK_HIT:
            if (p->attack_timer == 12)
            {
                p->move_hit = 0;
                p_attack_reset(p);
                if (!p->double_jump && (p->input & P_INPUT_UP))
                {
                    if (p->input & P_INPUT_LEFT)
                    {
                        p->xdir = -1;
                    }
                    else if (p->input & P_INPUT_RIGHT)
                    {
                        p->xdir = 1;
                    }
                    p->xvel = P_XVEL_STEER;
                    p->yvel = P_JUMPVEL_D;
                    p->double_jump = 1;
                    p->state = P_STATE_JUMP;
                    p->last_state = P_STATE_FALL;
                }
                else
                {
                    if (p->last_state == P_STATE_JUMP)
                        p->state = P_STATE_JUMP;
                    else if (p->last_state == P_STATE_FALL)
                        p->state = P_STATE_FALL;
                    else 
                        p->state = P_STATE_FALL;
                }
            }
            break;
    }

    if (p->attack_state != P_ATTACK_HIT)
    {
        p->x += p->xdir * p->xvel * delta;
        p->yvel += P_YVEL * delta;
        p->y += p->yvel * delta;
    }

    if (p->facing == P_FACE_RIGHT)
        p->a_hbox.x = p->r.x + p->r.w;
    else
        p->a_hbox.x = p->r.x - p->a_hbox.w;

    p->a_hbox.y = p->r.y + p->r.h - p->a_hbox.h;

    if (p->y + p->r.h >= W_HEIGHT)
    {
        p->move_hit = 0;
        p_attack_reset(p);
        p->attack_air = 0;
        p->double_jump = 0;
        p->xvel = P_XVEL;
        p->yvel = 0;
        p->y = W_HEIGHT - p->r.h;
        p->last_state = P_STATE_FALL;
        p->state = P_STATE_STAND;
    }
}

void p_update_kick_air_hit()
{

}

void p_update(Player *p, Player *opp, float delta)
{
    switch (p->state)
    {
        default: break;
        case P_STATE_STAND:
            p_update_state_stand(p, delta);
            p_side_adjust(p, opp->col);
            break;
        case P_STATE_CROUCH:
            p_update_state_crouch(p, delta);
            break;
        case P_STATE_JUMP:
            p_update_state_jump(p, delta);
            break;
        case P_STATE_FALL:
            p_update_state_fall(p, delta);
            break;
        case P_STATE_KNOCKDOWN:
            p_update_state_knockdown(p, delta);
            break;
        case P_STATE_BLOCK:
            p_update_state_block(p, 8, delta);
            break;
        case P_STATE_BLOCK_LOW:
            p_update_state_block_low(p, 8, delta);
            break;
        case P_STATE_BLOCK_AIR:
            p_update_state_block_air(p, 8, delta);
            break;
        case P_STATE_DASH_G:
            p_update_state_dash_g(p, delta);
            break;
        case P_STATE_DASH_G_BW:
            p_update_state_dash_g_bw(p, delta);
            break;
        case P_STATE_DASH_A:
            p_update_state_dash_a(p, delta);
            break;
        case P_STATE_DASH_A_BW:
            p_update_state_dash_a_bw(p, delta);
            break;
        case P_STATE_PUNCH:
            p_update_punch(p, delta);
            break;
        case P_STATE_HIT_PUNCH:
            p_update_hit_punch(p, opp->facing, delta);
            break;
        case P_STATE_KICK:
            p_update_kick(p, opp->corner, delta);
            break;
        case P_STATE_HIT_KICK:
            p_update_hit_kick(p, delta);
            break;
        case P_STATE_KICK_LOW:
            p_update_kick_low(p, opp->hit, opp->corner, delta);
            break;
        case P_STATE_HIT_KICK_LOW:
            p_update_hit_kick_low(p, delta);
            break;
        case P_STATE_KICK_AIR:
            p_update_kick_air(p, opp->hit, opp->corner, delta);
            break;
    }

    if (opp->attack)
    {
        if (opp->attack_state == P_ATTACK_ACTIVE)
        {
            if (!p->hit && !opp->move_hit)
            {
                unsigned char hit = 0;
                if (p_coll(p->hbox_high, opp->a_hbox))
                {
                    switch (opp->state)
                    {
                        default: break;
                        case P_STATE_PUNCH:
                            hit = 1;
                            p->xdir = opp->facing ? 1 : -1;
                            p->state = P_STATE_HIT_PUNCH;
                            break;
                        case P_STATE_KICK:
                        case P_STATE_KICK_AIR:
                            hit = 1;
                            p->xdir = opp->facing ? 1 : -1;
                            p->state = P_STATE_HIT_KICK;
                            break;
                    }
                }
                else if (p_coll(p->hbox_mid, opp->a_hbox))
                {
                    switch (opp->state)
                    {
                        default: break;
                        case P_STATE_PUNCH:
                            hit = 1;
                            p->xdir = opp->facing ? 1 : -1;
                            p->state = P_STATE_HIT_PUNCH;
                            break;
                        case P_STATE_KICK:
                        case P_STATE_KICK_AIR:
                            hit = 1;
                            p->xdir = opp->facing ? 1 : -1;
                            p->state = P_STATE_HIT_KICK;
                            break;
                    }
                }
                else if (p_coll(p->hbox_low, opp->a_hbox))
                {
                    switch (opp->state)
                    {
                        default: break;
                        case P_STATE_PUNCH:
                            hit = 1;
                            p->xdir = opp->facing ? 1 : -1;
                            p->state = P_STATE_HIT_PUNCH;
                            break;
                        case P_STATE_KICK:
                        case P_STATE_KICK_AIR:
                            hit = 1;
                            p->xdir = opp->facing ? 1 : -1;
                            p->state = P_STATE_HIT_KICK;
                            break;
                        case P_STATE_KICK_LOW:
                            hit = 1;
                            if (p->state == P_STATE_STAND)
                            {
                                p->knockdown_begin = SDL_GetTicks64();
                                p->r.h = 40;
                                p->xdir = 0;
                                p->yvel = -1;
                                p->state = P_STATE_KNOCKDOWN;
                            }
                            else if (p->state == P_STATE_CROUCH)
                            {
                                p->xdir = opp->facing ? 1 : -1;
                                p->state = P_STATE_HIT_KICK_LOW;
                            }
                            break;
                    }
                }
                if (hit)
                {
                    opp->move_hit = 1;
                    p->hit = 1;
                    p_attack_reset(p);
                    p_dash_cancel(p);
                    p_freeze_reset(p);
                }
            }
        }
    }

    // separate check for x and y collision (?)
    if (opp->xdir)
    {
        float testx = opp->xdir * opp->xvel * delta;
        if (p_coll(p->r, opp->r))
        {
            if (p->x + testx + p->r.w >= W_WIDTH)
            {
                opp->x = p->r.x - p->r.w;
            }
            else if (p->x + testx <= 0)
            {
                opp->x = p->r.x + p->r.w;
            }
            else 
            {
                p->x += testx;

                if (opp->x > p->x)
                    opp->x = p->x + p->r.w;
                else if (opp->x < p->x)
                    opp->x = p->x - p->r.w;
            }
        }
    }

    if (p->x < 40 || p->x + p->r.w > W_WIDTH - 40)
    {
        p->corner = 1;
    }
    else p->corner = 0;

    if (p->x <= 0) p->x = 0;
    else if (p->x + p->r.w >= W_WIDTH)
        p->x = W_WIDTH - p->r.w;

    p->r.x = (int)p->x;
    p->col.x = p->r.x + (p->r.w >> 1);
    
    if (p->state == P_STATE_CROUCH 
    || p->state == P_STATE_KICK_LOW)
    {
        p->r.y = (int)p->y + 20;
        p->col.y = p->y + 20;
    }
    else
    {
        p->r.y = (int)p->y;
        p->col.y = p->r.y;
    }

    p->hbox_high.x = p->r.x + (p->r.w >> 1) - 10;
    p->hbox_high.y = p->r.y;
    p->hbox_mid.x = p->r.x + (p->r.w >> 1) - 10;
    p->hbox_mid.y = p->r.y + 30;
    p->hbox_low.x = p->r.x + (p->r.w >> 1) - 10;
    p->hbox_low.y = p->r.y + 60;
}

void p_key_down(Player *p, SDL_Event e)
{
    switch (e.key.keysym.sym)
    {
        default: break;
        case SDLK_KP_8:
            p->input += P_INPUT_UP;
            queue_put(p->i_queue, P_INPUT_UP);
            switch (p->state)
            {
                default: break;
                case P_STATE_JUMP:
                case P_STATE_FALL:
                case P_STATE_DASH_A:
                case P_STATE_DASH_A_BW:
                    if (!p->double_jump)
                    {
                        if (p->input & P_INPUT_LEFT)
                        {
                            if (p->xdir == 1)
                                p->xvel = P_XVEL_BW;
                            p->xdir = -1;
                        }
                        else if (p->input & P_INPUT_RIGHT)
                        {
                            if (p->xdir == -1)
                                p->xvel = P_XVEL_BW;
                            p->xdir = 1;
                        }
                        else p->xdir = 0;

                        p->yvel = P_JUMPVEL_D;
                        p->double_jump = 1;
                        p->last_state = p->state;
                        p->state = P_STATE_JUMP;
                    }
                    break;
                case P_STATE_DASH_G:
                case P_STATE_DASH_G_BW:
                    p_dash_cancel(p);
                    p->yvel = P_JUMPVEL;
                    p->last_state = p->state;
                    p->state = P_STATE_JUMP;
                    break;
            }
            break;
        case SDLK_KP_4:
            p->input += P_INPUT_LEFT;
            queue_put(p->i_queue, P_INPUT_LEFT);
            if (!p->dash)
            {
                switch (p->state)
                {
                    default: break;
                    case P_STATE_JUMP:
                    case P_STATE_FALL:
                        if (p->xdir > 0)
                        {
                            switch (p->xvel)
                            {
                                default: break;
                                case P_XVEL_JUMP:
                                case P_XVEL_BW:
                                    p->xvel = P_XVEL_COL;
                                    break;
                                case P_XVEL_COL:
                                    p->xvel = P_XVEL_STEER;
                                    break;
                                case P_XVEL_STEER:
                                    p->xvel = P_XVEL_COL;
                                    break;
                            }
                        }
                    case P_STATE_STAND:
                        if (!p->attack && !p->attack_air)
                        {
                            if (p->dash_dir == -1)
                            {
                                if ((SDL_GetTicks64() - p->dash_counter) <= 200) 
                                {
                                    p->dash_begin = SDL_GetTicks64();
                                    p->dash_counter = 0;
                                    p->dash = 1;
                                    p->xdir = -1;
                                    p->yvel = 0;
                                    p->freeze_begin = SDL_GetTicks64();
                                    p->freeze = 1;
                                    if (p->facing == P_FACE_RIGHT)
                                    {
                                        p->xvel = P_DASHVEL_BW;
                                        p->state = !p->state ? P_STATE_DASH_G_BW : P_STATE_DASH_A_BW;
                                    }
                                    else
                                    {
                                        p->xvel = P_DASHVEL;
                                        p->state = !p->state ? P_STATE_DASH_G : P_STATE_DASH_A;
                                    }
                                }
                            }
                            p->dash_counter = SDL_GetTicks64();
                        }
                        break;
                }
            }
            p->dash_dir = -1;
            break;
        case SDLK_KP_5:
            p->input += P_INPUT_DOWN;
            queue_put(p->i_queue, P_INPUT_DOWN);
            break;
        case SDLK_KP_6:
            p->input += P_INPUT_RIGHT;
            queue_put(p->i_queue, P_INPUT_RIGHT);
            if (!p->dash)
            {
                switch (p->state)
                {
                    default: break;
                    case P_STATE_JUMP:
                    case P_STATE_FALL:
                        if (p->xdir < 0)
                        {
                            switch (p->xvel)
                            {
                                default: break;
                                case P_XVEL_JUMP:
                                case P_XVEL_BW:
                                    p->xvel = P_XVEL_COL;
                                    break;
                                case P_XVEL_COL:
                                    p->xvel = P_XVEL_STEER;
                                    break;
                                case P_XVEL_STEER:
                                    p->xvel = P_XVEL_COL;
                                    break;
                            }
                        }
                    case P_STATE_STAND:
                        if (!p->attack && !p->attack_air)
                        {
                            if (p->dash_dir == 1)
                            {
                                if ((SDL_GetTicks64() - p->dash_counter) <= 200) 
                                {
                                    p->dash_begin = SDL_GetTicks64();
                                    p->dash_counter = 0;
                                    p->dash = 1;
                                    p->xdir = 1;
                                    p->yvel = 0;
                                    p->freeze_begin = SDL_GetTicks64();
                                    p->freeze = 1;
                                    if (p->facing == P_FACE_RIGHT)
                                    {
                                        p->xvel = P_DASHVEL;
                                        p->state = !p->state ? P_STATE_DASH_G : P_STATE_DASH_A;
                                    }
                                    else
                                    {
                                        p->xvel = P_DASHVEL_BW;
                                        p->state = !p->state ? P_STATE_DASH_G_BW : P_STATE_DASH_A_BW;
                                    }
                                }
                            }
                            p->dash_counter = SDL_GetTicks64();
                        }
                        break;
                }
            }
            p->dash_dir = 1;
            break;
        case SDLK_SPACE:
            p->input += P_INPUT_KICK;
            queue_put(p->i_queue, P_INPUT_KICK);
            switch (p->state)
            {
                default: break;
                case P_STATE_JUMP:
                case P_STATE_FALL:
                    if (!p->freeze)
                    {
                        if (!p->attack && !p->attack_air)
                        {
                            p_dash_cancel(p);
                            p->attack = 1;
                            p->attack_air = 1;
                            p->last_state = p->state;
                            p->state = P_STATE_KICK_AIR;
                        }
                    }
                    break;
                case P_STATE_DASH_A:
                case P_STATE_DASH_A_BW:
                    if (!p->freeze)
                    {
                        if (!p->attack && !p->attack_air)
                        {
                            p_dash_cancel(p);
                            p->attack = 1;
                            p->attack_air = 1;
                            p->last_state = p->state;
                            p->state = P_STATE_KICK_AIR;
                        }
                    }
                    break;
                case P_STATE_STAND:
                case P_STATE_DASH_G:
                case P_STATE_DASH_G_BW:
                    if (!p->freeze && !p->attack)
                    {
                        p_dash_cancel(p);
                        p->attack = 1;
                        p->last_state = p->state;
                        p->state = P_STATE_KICK;
                    }
                    break;
                case P_STATE_CROUCH:
                    if (!p->freeze && !p->attack)
                    {
                        p->attack = 1;
                        p->r.h = 60;
                        p->col.h = 60;
                        p->last_state = p->state;
                        p->state = P_STATE_KICK_LOW;
                    }
                    break;
            }
            break;
    }
}

void p_key_up(Player *p, SDL_Event e)
{
    switch (e.key.keysym.sym)
    {
        default: break;
        case SDLK_KP_8:
            p->input -= P_INPUT_UP;
            queue_remove(p->i_queue, P_INPUT_UP);
            break;
        case SDLK_KP_4:
            p->input -= P_INPUT_LEFT;
            queue_remove(p->i_queue, P_INPUT_LEFT);
            break;
        case SDLK_KP_5:
            p->input -= P_INPUT_DOWN;
            queue_remove(p->i_queue, P_INPUT_DOWN);
            break;
        case SDLK_KP_6:
            p->input -= P_INPUT_RIGHT;
            queue_remove(p->i_queue, P_INPUT_RIGHT);
            break;
        case SDLK_SPACE:
            p->input += P_INPUT_KICK;
            queue_put(p->i_queue, P_INPUT_KICK);
            break;
    }
}

void p_pad_axis(Player *p, SDL_Event e)
{

}

void p_pad_button_down(Player *p, SDL_Event e)
{
    switch (e.jbutton.button)
    {
        default: break;
    }
}

void p_pad_button_up(Player *p, SDL_Event e)
{
    switch (e.jbutton.button)
    {
        default: break;
    }
}

void p_input(Player *p, SDL_Event e, int opt_state)
{
    if (p->input_method == P_METHOD_KEYBOARD)
    {
        if (!e.key.repeat)
        {
            switch (e.type)
            {
                default: break;
                case SDL_KEYDOWN:
                    p_key_down(p, e);
                    break;
                case SDL_KEYUP:
                    p_key_up(p, e);
                    break;
            }
        }
    }
    else if (p->input_method == P_METHOD_GAMEPAD)
    {
        switch (e.type)
        {
            default: break;
            case SDL_JOYAXISMOTION:
                p_pad_axis(p, e);
                break;
            case SDL_JOYBUTTONDOWN:
                p_pad_button_down(p, e);
                break;
            case SDL_JOYBUTTONUP:
                p_pad_button_up(p, e);
                break;
        }
    }
    else if (p->input_method == P_METHOD_DUMMY)
    {
        switch (opt_state)
        {
            default: break;
            case OPTION_STATE_STAND:
                p->input = 0;
                break;
            case OPTION_STATE_CROUCH:
                p->input = P_INPUT_DOWN;
                break;
            case OPTION_STATE_JUMP:
                p->input = P_INPUT_UP;
                break;
        }
    }
}

void g_handle_inputs(Game *g, SDL_Event e)
{
    switch (g->state)
    {
        default: break;
        case G_STATE_MENU:
            if (e.key.keysym.sym == SDLK_KP_8)
            {
                if (--g->opt_select == 255) 
                    g->opt_select = G_OPTION_P_KICK_RECOVERY;
            }
            else if (e.key.keysym.sym == SDLK_KP_2)
            {
                if (++g->opt_select > (OPTIONS_COUNT - 1)) 
                    g->opt_select = G_OPTION_E_STATE;
            }
            else if (e.key.keysym.sym == SDLK_LEFT)
            {
                switch (g->opt_select)
                {
                    default: break;
                    case G_OPTION_E_STATE:
                        if (--g->opt_state == 255)
                        {
                            g->opt_state = OPTION_STATE_JUMP;
                        }
                        break;
                    case G_OPTION_E_BLOCK:
                        g->players[PLAYER_TWO].block = !g->players[PLAYER_TWO].block;
                        break;
                    case G_OPTION_P_KICK_START:
                        if (--g->move_list.kick.startup < 1)
                            g->move_list.kick.startup = 1;
                        break;
                    case G_OPTION_P_KICK_ACTIVE:
                        if (--g->move_list.kick.active < 1)
                            g->move_list.kick.active = 1;
                        break;
                    case G_OPTION_P_KICK_RECOVERY:
                        if (--g->move_list.kick.recovery < 1)
                            g->move_list.kick.recovery = 1;
                        break;
                }
            }
            else if (e.key.keysym.sym == SDLK_RIGHT)
            {
                switch (g->opt_select)
                {
                    default: break;
                    case G_OPTION_E_STATE:
                        if (++g->opt_state > 2) 
                            g->opt_state = OPTION_STATE_STAND;
                        break;
                    case G_OPTION_E_BLOCK:
                        g->players[PLAYER_TWO].block = !g->players[PLAYER_TWO].block;
                        break;
                    case G_OPTION_P_KICK_START:
                        if (++g->move_list.kick.startup > 60)
                            g->move_list.kick.startup = 60;
                        break;
                    case G_OPTION_P_KICK_ACTIVE:
                        if (++g->move_list.kick.active > 60)
                            g->move_list.kick.active = 60;
                        break;
                    case G_OPTION_P_KICK_RECOVERY:
                        if (++g->move_list.kick.recovery > 60)
                            g->move_list.kick.recovery = 60;
                        break;
                }
            }
            else if (e.key.keysym.sym == SDLK_ESCAPE)
            {
                //save_to_file(&g);
                g->state = G_STATE_PLAY;
            }
            break;
        case G_STATE_PLAY:
            if (e.key.keysym.sym == SDLK_ESCAPE)
            {
                g->state = G_STATE_MENU;
            }
            else if (e.key.keysym.sym == SDLK_RETURN)
            {
                //read_from_file(g);
                p_reset_pos(
                    &g->players[PLAYER_ONE], 
                    W_WIDTH >> 2);
                p_reset_pos(
                    &g->players[PLAYER_TWO], 
                    W_WIDTH - (W_WIDTH >> 2) - g->players[PLAYER_TWO].r.w);
            }
            break;
    }
}

int main(int argc, char const *argv[])
{
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_JOYSTICK) < 0) 
    {
        printf("Could not init SDL: %s\n", SDL_GetError());
        goto quit;
    }

    if(!IMG_Init(IMG_INIT_PNG))
    {
        printf("SDL_image could not initialize! SDL_image Error: %s\n", IMG_GetError());
        goto quit;
    }

    SDL_Window *w = SDL_CreateWindow(
        "Fighter", 
        SDL_WINDOWPOS_UNDEFINED, 
        SDL_WINDOWPOS_UNDEFINED, 
        W_WIDTH, W_HEIGHT, 
        SDL_WINDOW_SHOWN);

    if (w == NULL)
    {
        printf("Could not create SDL window, %s\n", SDL_GetError());
        goto quit;
    }

    SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);

    if (r == NULL)
    {
        printf("Could not create SDL renderer, %s\n", SDL_GetError());
        goto quit;
    }

    if (SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND) < 0)
    {
        printf("Could not set render blend mode, %s\n", SDL_GetError());
        goto quit;
    }

    FC_Font *font = FC_CreateFont();
    FC_LoadFont(font, r, "ASCII.ttf", 16, FC_MakeColor(255, 255, 255, 255), TTF_STYLE_NORMAL);

    if (font == NULL)
    {
        printf("Could not create font.\n");
        goto quit;
    }

    int num_joysticks;
    SDL_GameController **controllers = NULL;

    if ((num_joysticks = SDL_NumJoysticks()) > 0)
    {
        controllers = calloc(num_joysticks, sizeof(SDL_GameController*));
        g_open_controllers(controllers, num_joysticks);
    }
    else
    {
        if (num_joysticks < 0)
        {
            printf("Could not get number of joysticks, %s\n", SDL_GetError());
        }
    }

    Game g;
    g_init(&g);

    g.players = calloc(2, sizeof(Player));

    if (g.players != NULL)
    {
        p_init(&g.players[PLAYER_ONE], &g.move_list, W_WIDTH >> 2);
        p_init(&g.players[PLAYER_TWO], &g.move_list, W_WIDTH - (W_WIDTH >> 2) - 40);

        g.players[PLAYER_TWO].input_method = P_METHOD_DUMMY;
    }
    else 
    {
        printf("could not init players!\n");
        goto quit;
    }

    SDL_Event e;
    Uint64 last_timer = SDL_GetTicks64();

    SDL_Rect q = {
        .x = (W_WIDTH >> 1) - 200, 
        .y = (W_HEIGHT >> 1) - 200,
        .w = 400,
        .h = 400
    };

    g.state = G_STATE_PLAY;

    while(!g.quit)
    {
        Uint64 start = SDL_GetPerformanceCounter();

        g.players[PLAYER_ONE].last_input = g.players[PLAYER_ONE].input;
        g.players[PLAYER_TWO].last_input = g.players[PLAYER_TWO].input;

        while(SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT) 
            {
                g.quit = 1;
                break;
            }
            else if (e.type == SDL_KEYDOWN && !e.key.repeat)
            {
                g_handle_inputs(&g, e);
            }
            p_input(&g.players[PLAYER_ONE], e, g.opt_state);
            p_input(&g.players[PLAYER_TWO], e, g.opt_state);
        }
        
        Uint64 timer = SDL_GetTicks64();
        float delta = (timer - last_timer) / 1000.0f;

        if (g.players[PLAYER_ONE].hit || g.players[PLAYER_TWO].hit)
        {
            //freeze 
            if (++g.g_hit_timer < 3)
            {
                g.game_freeze = 1;
            }
            else 
            {
                g.game_freeze = 0;
            }
        }
        else g.g_hit_timer = 0;
        
        if (!g.game_freeze)
        {
            p_update(&g.players[PLAYER_ONE], &g.players[PLAYER_TWO], delta);
            p_update(&g.players[PLAYER_TWO], &g.players[PLAYER_ONE], delta);
        }

        last_timer = timer;

        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderClear(r);

        if (g.game_freeze)
        {
            SDL_SetRenderDrawColor(r, 255, 255, 0, 32);
            SDL_Rect bg = {.x = 0, .y = 0, .w = W_WIDTH, .h = W_HEIGHT};
            SDL_RenderFillRect(r, &bg);
        }

        p_render(r, g.players[PLAYER_ONE]);
        p_render(r, g.players[PLAYER_TWO]);

        switch (g.state)
        {
            default: break;
            case G_STATE_MENU:
                SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
                SDL_RenderFillRect(r, &q);
                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                SDL_RenderDrawRect(r, &q);

                FC_Draw(font, r, q.x + 20, q.y + 20, "OPTIONS");
                FC_Draw(font, r, q.x + 20, q.y + 60, "Dummy State");
                FC_Draw(font, r, q.x + 20, q.y + 80, "Dummy Block");
                FC_Draw(font, r, q.x + 20, q.y + q.h - 80, "Kick Startup");
                FC_Draw(font, r, q.x + 20, q.y + q.h - 60, "Kick Attack");
                FC_Draw(font, r, q.x + 20, q.y + q.h - 40, "Kick Recovery");

                int x = q.x + 10, y;

                switch (g.opt_select)
                {
                    default: break;
                    case G_OPTION_E_STATE:
                        y = q.y + 60;
                        break;
                    case G_OPTION_E_BLOCK:
                        y = q.y + 80;
                        break;
                    case G_OPTION_P_KICK_START:
                        y = q.y + q.h - 80;
                        break;
                    case G_OPTION_P_KICK_ACTIVE:
                        y = q.y + q.h - 60;
                        break;
                    case G_OPTION_P_KICK_RECOVERY:
                        y = q.y + q.h - 40;
                        break;
                }

                FC_Draw(font, r, x, y, ">");

                g_render_option_state(r, font, q, g.opt_state);
                g_render_option_block(r, font, q, g.players[PLAYER_TWO].block);
                g_render_option_kick_startup(r, font, q, g.move_list.kick.startup);
                g_render_option_kick_active(r, font, q, g.move_list.kick.active);
                g_render_option_kick_recovery(r, font, q, g.move_list.kick.recovery);
                break;
            case G_STATE_PLAY:
                break;
        }

        SDL_RenderPresent(r);

        Uint64 end = SDL_GetPerformanceCounter();
        float elapsed = (end - start) / (float)SDL_GetPerformanceFrequency() * 1000.0f;
        SDL_Delay(SDL_floor(FPS_CAP_MS - elapsed));
    }

quit:
    if (controllers != NULL)
    {
        g_close_controllers(controllers, num_joysticks);
        free(controllers);
    }

    g_close(&g);
    FC_FreeFont(font);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    IMG_Quit();
    SDL_Quit();

    return 0;
}

