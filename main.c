#include "SDL_FontCache.h"

#define W_WIDTH 640
#define W_HEIGHT 480
#define FPS_CAP_MS 16.66666603F

#define P_XVELOCITY     250
#define P_XVELOCITY_BW  175
#define P_XVEL_DOUBLE   500
#define P_DASHVEL       400
#define P_DASHVEL_BW    300
#define P_YVELOCITY     900
#define P_JUMPVEL      -500
#define P_JUMPVEL_D    -350

#define PLAYER_ONE 0
#define PLAYER_TWO 1

enum Player_Method
{
    P_METHOD_KEYBOARD,
    P_METHOD_GAMEPAD
};

enum Player_Input
{
    P_INPUT_LEFT = 1,
    P_INPUT_UP = 2,
    P_INPUT_RIGHT = 4,
    P_INPUT_DOWN = 8,
    P_INPUT_PUNCH = 16,
    P_INPUT_KICK = 32
};

enum Player_State
{
    P_STATE_STAND,
    P_STATE_DUCK,
    P_STATE_JUMP,
    P_STATE_FALL,
    P_STATE_KNOCKDOWN,
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
    P_STATE_HIT_KICK_HIGH
};

enum Player_Facing
{
    P_FACE_LEFT,
    P_FACE_RIGHT
};

enum Player_Attack_State
{
    P_ATTACK_STARTUP,
    P_ATTACK_ACTIVE,
    P_ATTACK_RECOVERY,
    P_ATTACK_HIT
};

typedef struct Player
{
    SDL_Rect r, col, hbox_mid, hbox_high, hbox_low, a_hbox;
    enum Player_Method input_method;
    unsigned char   input, last_input, 
                    corner:1, double_jump:1, block:1, dash:1, freeze:1, attack:1, hit:1;
    char state, attack_state, facing, xdir, i_queue[8];
    int freeze_timer, attack_timer, hit_timer;
    float x, y, xvel, yvel;
    Uint64  dash_begin, dash_end, dash_counter, 
            freeze_begin, freeze_end,
            attack_recovery, attack_startup, attack_active,
            knockdown_begin;
} Player;

typedef struct Game
{
    Player players[2];

    const int   p_xvel, p_xvel_backwards, p_yvel, 
                p_jumpvel, p_double_jumpvel;

    int         num_joysticks;
} Game;

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

void read_from_file(Game *g)
{
    FILE *f;
    char buf[32];
    int c, i = 0;

    if (!(f = fopen("edit.ini", "a+"))) 
    {
        printf("could not open/append to file: %s\n", "edit.ini");
        return;
    }

    while (!feof(f))
    {
        c = fgetc(f);
        if (c == '=')
        {
            if (strcmp(buf, "player_xvel"))
            {
                printf("setting: %s\n", buf);
                while ((c = fgetc(f)) != '\n')
                {
                    buf[i] = c;
                    i++;
                }
                int n = atoi(&buf[i]);
                printf("setting value: %d\n", n);
                break;
            }
        }
        buf[i] = c;
        i++;
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

void p_render(SDL_Renderer *r, Player p)
{
    // draw player 
    if (p.hit)
    {
        SDL_SetRenderDrawColor(r, 255, 255, 0, 255);
    }
    else 
    {
        if (p.facing == P_FACE_RIGHT) 
            SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
        else
            SDL_SetRenderDrawColor(r, 0, 0, 255, 255);
    }

    SDL_RenderFillRect(r, &p.r);

    // draw hitboxes
    SDL_SetRenderDrawColor(r, 0, 255, 0, 128);
    SDL_RenderFillRect(r, &p.col);

    SDL_SetRenderDrawColor(r, 0, 0, 255, 255);
    SDL_RenderDrawRect(r, &p.hbox_high);
    SDL_RenderDrawRect(r, &p.hbox_mid);
    SDL_RenderDrawRect(r, &p.hbox_low);

    SDL_SetRenderDrawColor(r, 0, 0, 255, 128);
    SDL_RenderFillRect(r, &p.hbox_high);
    SDL_RenderFillRect(r, &p.hbox_mid);
    SDL_RenderFillRect(r, &p.hbox_low);

    if (p.state == P_STATE_KICK)
    {
        if (p.attack_state == P_ATTACK_ACTIVE)
        {
            SDL_SetRenderDrawColor(r, 0, 255, 0, 128);
            SDL_RenderFillRect(r, &p.a_hbox);
        }
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

void p_init(Player *p, int xpos)
{
    for (int i = 0; i < 8; i++)
        p->i_queue[i] = 0;

    p->input_method = 0;
    p->input = 0;
    p->last_input = 0;
    p->corner = 0;
    p->double_jump = 0;
    p->x = xpos;
    p->y = W_HEIGHT - 80;
    //p->r.x = (int)p->x;
    //p->r.y = (int)p->y;
    p->r.w = 40;
    p->r.h = 80;
    p->col.w = 1;
    p->col.h = 80;
    //p->col.x = p->r.x + (p->r.w >> 1);
    //p->col.y = p->r.y;
    p->a_hbox.w = 60;
    p->a_hbox.h = 30;
    p->hbox_high.w = 40;
    p->hbox_high.h = 25;
    p->hbox_mid.w = 40;
    p->hbox_mid.h = 25;
    p->hbox_low.w = 40;
    p->hbox_low.h = 25;
    //p->hitbox.x = p->r.x;
    //p->hitbox.y = p->r.y;
    p->facing = 0;
    p->xdir = 0;
    p->xvel = P_XVELOCITY;
    p->yvel = 0;
    p->hit_timer = 0;
    p->hit = 0;
    p->state = P_STATE_STAND;
    p_freeze_reset(p);
    p_dash_cancel(p);
    p_attack_reset(p);
    p->knockdown_begin = 0;
}

void p_reset_pos(Player *p, int xpos)
{
    p->last_input = 0;
    p->corner = 0;
    p->double_jump = 0;
    p->xdir = 0;
    p->xvel = P_XVELOCITY;
    p->yvel = 0;
    p->r.h = 80;
    p->col.h = 80;
    p->x = xpos;
    p->y = W_HEIGHT - 80;
    p_freeze_reset(p);
    p_dash_cancel(p);
    p_attack_reset(p);
    p->hit_timer = 0;
    p->hit = 0;
    p->knockdown_begin = 0;
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
        if (p->state == P_STATE_STAND)
            p->facing = P_FACE_RIGHT;
    }
    else if (p->col.x > opp_rect.x)
    {
        if (p->state == P_STATE_STAND)
            p->facing = P_FACE_LEFT;
    }
}

int p_check_jump(Player *p)
{
    if (p->input & P_INPUT_UP 
    && !(p->last_input & P_INPUT_UP))
    {
        p->double_jump = 1;

        if (p->input & P_INPUT_LEFT)
            p->xdir = -1;
        else if (p->input & P_INPUT_RIGHT)
            p->xdir = 1;
        else 
            p->xdir = 0;

        return 1;
    }
    return 0;
}

void p_update_state_stand(Player *p, float delta)
{
    if (p->input & P_INPUT_DOWN)
    {
        p->r.h = 40;
        p->col.h = 40;
        p->state = P_STATE_DUCK;
        return;
    }
    else if (p->input & P_INPUT_UP)
    {
        p->yvel = P_JUMPVEL;
        p->state = P_STATE_JUMP;
    }

    if (p->input & P_INPUT_LEFT) p->xdir = -1;
    else if (p->input & P_INPUT_RIGHT) p->xdir = 1;
    else p->xdir = 0;

    if (p->facing == P_FACE_RIGHT)
    {
        if (p->xdir < 0)
            p->x += p->xdir * P_XVELOCITY_BW * delta;
        else if (p->xdir > 0)
            p->x += p->xdir * P_XVELOCITY * delta;
    }
    else
    {
        if (p->xdir < 0)
            p->x += p->xdir * P_XVELOCITY * delta;
        else if (p->xdir > 0)
            p->x += p->xdir * P_XVELOCITY_BW * delta;
    }
}

void p_update_state_duck(Player *p, float delta)
{
    if (!(p->input & P_INPUT_DOWN))
    {
        p->r.h = 80;
        p->col.h = 80;
        p->state = P_STATE_STAND;
    }
}

void p_update_state_jump(Player *p, float delta)
{
    if (!p->double_jump)
    {
        if (p_check_jump(p)) 
        {
            p->xvel = P_XVELOCITY;
            p->yvel = P_JUMPVEL_D;
            p->state = P_STATE_JUMP;
            return;
        }
    }

    p->x += p->xdir * p->xvel * delta;

    if ((p->yvel += P_YVELOCITY * delta) >= 0) 
        p->state = P_STATE_FALL;

    p->y += p->yvel * delta;
}

void p_update_state_fall(Player *p, float delta)
{
    if (!p->double_jump)
    {
        if (p_check_jump(p)) 
        {
            p->xvel = P_XVELOCITY;
            p->yvel = P_JUMPVEL_D;
            p->state = P_STATE_JUMP;
            return;
        }
    }

    p->x += p->xdir * p->xvel * delta;
    p->yvel += P_YVELOCITY * delta;
    p->y += p->yvel * delta;

    if (p->y + p->r.h >= W_HEIGHT)
    {
        p->double_jump = 0;
        p->xvel = P_XVELOCITY;
        p->yvel = 0;
        p->y = W_HEIGHT - p->r.h;
        p_dash_cancel(p);
        p->state = P_STATE_STAND;
    }
}

void p_update_state_knockdown(Player *p, float delta)
{
    p->y += p->yvel;
    p->yvel += 0.1f;

    if (p->y + p->r.h >= W_HEIGHT)
    {
        p->y = W_HEIGHT - p->r.h;
    }

    if ((SDL_GetTicks64() - p->knockdown_begin) >= 1000) 
    {
        p->knockdown_begin = 0;
        p->r.h = 80;
        p->y = W_HEIGHT - 80;
        p->state = P_STATE_STAND;
    }
}

void p_update_state_dash_g(Player *p, float delta)
{
    p->x += p->xdir * P_DASHVEL * delta;

    if (p->input & P_INPUT_UP)
    {
        p_dash_cancel(p);
        p->xvel = P_DASHVEL;
        p->yvel = P_JUMPVEL;
        p->state = P_STATE_JUMP;
        return;
    }

    if ((p->facing == P_FACE_RIGHT && p->input & P_INPUT_LEFT) 
    || (p->facing == P_FACE_LEFT && p->input & P_INPUT_RIGHT))
    {
        p_dash_cancel(p);
        p->state = P_STATE_STAND;
        return;
    }

    if ((SDL_GetTicks64() - p->dash_begin) >= 250) 
    {
        p_dash_cancel(p);
        p->xvel = P_XVELOCITY;
        p->state = P_STATE_STAND;
    }
}

void p_update_state_dash_g_bw(Player *p, float delta)
{
    p->x += p->xdir * P_DASHVEL * delta;

    if (p->input & P_INPUT_UP)
    {
        p_dash_cancel(p);
        p->xvel = P_DASHVEL;
        p->yvel = P_JUMPVEL;
        p->state = P_STATE_JUMP;
        return;
    }

    if ((SDL_GetTicks64() - p->dash_begin) >= 150) 
    {
        p_dash_cancel(p);
        p->xvel = P_XVELOCITY;
        p->state = P_STATE_STAND;
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
            return;
        }
    }

    p->x += p->xdir * P_DASHVEL * delta;

    if (!p->double_jump && (p->input & P_INPUT_UP))
    {
        p_dash_cancel(p);
        p->xvel = P_XVELOCITY;
        p->yvel = P_JUMPVEL_D;
        p->double_jump = 1;
        p->state = P_STATE_JUMP;
        return;
    }

    if ((SDL_GetTicks64() - p->dash_begin) >= 200) 
    {
        p->yvel = 0;
        p->state = P_STATE_FALL;
    }
}

void p_update_state_dash_a_bw(Player *p, float delta)
{
    if (p->freeze) 
    {
        if ((SDL_GetTicks64() - p->freeze_begin) >= (FPS_CAP_MS * 4))
        {
            p->freeze = 0; 
            p->freeze_begin = 0;
            p->dash_begin = SDL_GetTicks64();
            return;
        }
    }

    p->x += p->xdir * P_DASHVEL * delta;

    if (!p->double_jump && (p->input & P_INPUT_UP))
    {
        p_dash_cancel(p);
        p->xvel = P_XVELOCITY;
        p->yvel = P_JUMPVEL_D;
        p->double_jump = 1;
        p->state = P_STATE_JUMP;
        return;
    }
    
    if ((SDL_GetTicks64() - p->dash_begin) >= 100) 
    {
        p->yvel = 0;
        p->state = P_STATE_FALL;
    }
}

void p_update_punch(Player *p, float delta)
{
    
}

void p_update_hit_punch(Player *p, int face, float delta)
{

}

void p_update_kick(Player *p, int opp_hit, int opp_corner, float delta)
{
    ++p->attack_timer;

    switch (p->attack_state)
    {
        case P_ATTACK_STARTUP:
            if (p->attack_timer == 6)
            {
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_ACTIVE;
            }
            break;
        case P_ATTACK_ACTIVE:
            if (opp_hit)
            {
                if (opp_corner) 
                {
                    p->x += p->facing ? -1 : 1;
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
            if (p->attack_timer == 8)
            {
                p_attack_reset(p);
                p->state = P_STATE_STAND;
            }
            break;
        case P_ATTACK_HIT:
            if (p->attack_timer == 18)
            {
                p_attack_reset(p);
                p->state = P_STATE_STAND;
            }
            break;
    }

    if (p->facing == P_FACE_RIGHT)
    {
        p->a_hbox.x = p->r.x + (p->r.w >> 1);
        p->a_hbox.y = p->r.y + (p->r.h >> 1) - (p->a_hbox.h >> 1);
    }
    else
    {
        p->a_hbox.x = p->r.x + (p->r.w >> 1) - p->a_hbox.w;
        p->a_hbox.y = p->r.y + (p->r.h >> 1) - (p->a_hbox.h >> 1);
    }
}

void p_update_hit_kick(Player *p, int face, float delta)
{
    if (face == P_FACE_LEFT)
    {
        p->x -= 1;
    }
    else
    {
        p->x += 1;
    }

    ++p->hit_timer;

    if (p->hit_timer == 20)
    {
        p->hit = 0;
        p->hit_timer = 0;
        p->state = P_STATE_STAND;
    }
}

void p_update_kick_low(Player *p, int opp_hit, int opp_corner, float delta)
{
    ++p->attack_timer;

    switch (p->attack_state)
    {
        case P_ATTACK_STARTUP:
            if (p->attack_timer == 6)
            {
                p->attack_timer = 0;
                p->attack_state = P_ATTACK_ACTIVE;
            }
            break;
        case P_ATTACK_ACTIVE:
            if (opp_hit)
            {
                if (opp_corner) 
                {
                    p->x += p->facing ? -1 : 1;
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
                p->state = P_STATE_STAND;
            }
            break;
        case P_ATTACK_HIT:
            if (p->attack_timer == 60)
            {
                p_attack_reset(p);
                p->state = p->input & P_INPUT_DOWN ? P_STATE_DUCK : P_STATE_STAND;
            }
            break;
    }

    if (p->facing == P_FACE_RIGHT)
    {
        p->a_hbox.x = p->r.x + (p->r.w >> 1);
        p->a_hbox.y = p->r.y + (p->r.h >> 1) - (p->a_hbox.h >> 1);
    }
    else
    {
        p->a_hbox.x = p->r.x + (p->r.w >> 1) - p->a_hbox.w;
        p->a_hbox.y = p->r.y + (p->r.h >> 1) - (p->a_hbox.h >> 1);
    }
}

void p_update_hit_kick_low(Player *p, float delta)
{

}

void p_update(Player *p, Player opp, float delta)
{
    switch (p->state)
    {
        case P_STATE_STAND:
            p_update_state_stand(p, delta);
            break;
        case P_STATE_DUCK:
            p_update_state_duck(p, delta);
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
            p_update_hit_punch(p, opp.facing, delta);
            break;
        case P_STATE_KICK:
            p_update_kick(p, opp.hit, opp.corner, delta);
            break;
        case P_STATE_HIT_KICK:
            p_update_hit_kick(p, opp.facing, delta);
            break;
        case P_STATE_KICK_LOW:
            p_update_kick_low(p, opp.hit, opp.facing, delta);
            break;
        case P_STATE_HIT_KICK_LOW:
            if (p->block)
            {

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
                        p->knockdown_begin = 0;
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
                        p->knockdown_begin = 0;
                        p->r.h = 40;
                        p->yvel = -1;
                        p->state = P_STATE_KNOCKDOWN;
                    }
                }
            }
            break;
        case P_STATE_HIT_KICK_HIGH:
            break;
    }

    if (opp.attack)
    {
        if (opp.attack_state == P_ATTACK_ACTIVE)
        {
            if (!p->hit)
            {

                if (p_coll(p->hbox_high, opp.a_hbox))
                {
                    p->hit = 1;
                    switch (opp.state)
                    {
                        case P_STATE_PUNCH:
                            p->state = P_STATE_HIT_PUNCH;
                            break;
                        case P_STATE_KICK:
                            p->state = P_STATE_HIT_KICK;
                            break;
                    }
                }
                else if (p_coll(p->hbox_mid, opp.a_hbox))
                {
                    p->hit = 1;
                    switch (opp.state)
                    {
                        case P_STATE_PUNCH:
                            p->state = P_STATE_HIT_PUNCH;
                            break;
                        case P_STATE_KICK:
                            p->state = P_STATE_HIT_KICK;
                            break;
                    }
                }
                else if (p_coll(p->hbox_low, opp.a_hbox))
                {
                    p->hit = 1;
                    switch (opp.state)
                    {
                        case P_STATE_PUNCH:
                            p->state = P_STATE_HIT_PUNCH;
                            break;
                        case P_STATE_KICK:
                        case P_STATE_KICK_LOW:
                            if (p->state == P_STATE_STAND)
                            {
                                p->state = P_STATE_KNOCKDOWN;
                            }
                            else if (p->state == P_STATE_DUCK)
                            {
                                p->state = P_STATE_HIT_KICK_LOW;
                            }
                            break;
                    }
                }
            }
        }
    }

    if (p->x < 40 || p->x + p->r.w > W_WIDTH - 40)
    {
        p->corner = 1;
    }
    else p->corner = 0;

    // separate check for x and y collision (?)

    if (p_coll(p->r, opp.col))
    {
        if (opp.xdir < 0)
            p->x -= opp.xvel * delta;
        else if (opp.xdir > 0)
            p->x += opp.xvel * delta;
    }

    if (p->x <= 0) p->x = 0;
    else if (p->x + p->r.w >= W_WIDTH)
        p->x = W_WIDTH - p->r.w;

    p->r.x = (int)p->x;
    p->r.y = p->state == P_STATE_DUCK ? (int)p->y + 40 : (int)p->y;

    p->hbox_high.x = p->r.x;
    p->hbox_high.y = p->r.y;
    p->hbox_mid.x = p->r.x;
    p->hbox_mid.y = p->r.y + 25;
    p->hbox_low.x = p->r.x;
    p->hbox_low.y = p->r.y + 50;

    p->col.x = p->r.x + (p->r.w >> 1);
    p->col.y = p->state == P_STATE_DUCK ? p->y + 40 : p->r.y;

    p_side_adjust(p, opp.col);
}

void p_key_down(Player *p, SDL_Event e)
{
    switch (e.key.keysym.sym)
    {
        case SDLK_KP_8:
            p->input += P_INPUT_UP;
            queue_put(p->i_queue, P_INPUT_UP);
            break;
        case SDLK_KP_4:
            p->input += P_INPUT_LEFT;
            queue_put(p->i_queue, P_INPUT_LEFT);
            if (!p->dash)
            {
                switch (p->state)
                {
                    case P_STATE_STAND:
                    case P_STATE_JUMP:
                    case P_STATE_FALL:
                        if ((SDL_GetTicks64() - p->dash_counter) <= 200) 
                        {
                            p->dash_begin = SDL_GetTicks64();
                            p->dash_counter = 0;
                            p->dash = 1;
                            p->xdir = -1;
                            p->xvel = P_XVELOCITY;
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
                        p->dash_counter = SDL_GetTicks64();
                        break;
                }
            }
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
                    case P_STATE_STAND:
                    case P_STATE_JUMP:
                    case P_STATE_FALL:
                        if ((SDL_GetTicks64() - p->dash_counter) <= 200) 
                        {
                            p->dash_begin = SDL_GetTicks64();
                            p->dash_counter = 0;
                            p->dash = 1;
                            p->xdir = 1;
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
                        p->dash_counter = SDL_GetTicks64();
                        break;
                }
            }
            break;
        case SDLK_SPACE:
            p->input += P_INPUT_KICK;
            queue_put(p->i_queue, P_INPUT_KICK);
            switch (p->state)
            {
                case P_STATE_STAND:
                //case P_STATE_JUMP:
                //case P_STATE_FALL:
                case P_STATE_DASH_G:
                    if (!p->attack)
                    {
                        p_dash_cancel(p);
                        p_attack_reset(p);
                        p->attack = 1;
                        p->state = P_STATE_KICK;
                        //p->attack_startup = SDL_GetTicks64();
                    }
                    break;
                case P_STATE_DUCK:
                    if (!p->attack)
                    {
                        p_attack_reset(p);
                        p->attack = 1;
                        p->state = P_STATE_KICK_LOW;
                        //p->attack_startup = SDL_GetTicks64();
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
        
    }
}

void p_pad_button_up(Player *p, SDL_Event e)
{
    switch (e.jbutton.button)
    {
        
    }
}

void p_input(Player *p, SDL_Event e, int pnr)
{
    if (p->input_method == P_METHOD_KEYBOARD)
    {
        if (!e.key.repeat)
        {
            switch (e.type)
            {
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
}

int main(int argc, char const *argv[])
{
    //Game g;
    //read_from_file(&g);

    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS|SDL_INIT_JOYSTICK) < 0) 
    {
        printf("Could not init SDL: %s\n", SDL_GetError());
        return 0;
    }

    if(!IMG_Init(IMG_INIT_PNG))
    {
        printf("SDL_image could not initialize! SDL_image Error: %s\n", IMG_GetError());
        SDL_Quit();
        return 0;
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
        IMG_Quit();
        SDL_Quit();
        return 0;
    }

    SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);

    if (r == NULL)
    {
        printf("Could not create SDL renderer, %s\n", SDL_GetError());
        SDL_DestroyWindow(w);
        IMG_Quit();
        SDL_Quit();
        return 0;
    }

    if (SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND)  < 0)
    {
        printf("Could not set render blend mode, %s\n", SDL_GetError());
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(w);
        IMG_Quit();
        SDL_Quit();
        return 0;
    }

    SDL_Event e;
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

    Player p1, p2;
    p_init(&p1, W_WIDTH >> 2);
    p_init(&p2, W_WIDTH - (W_WIDTH >> 2) - 40);

    Uint64 last_timer = SDL_GetTicks64();
    unsigned char game_freeze = 0;
    int g_hit_timer = 0, quit = 0;

    while(!quit)
    {
        Uint64 start = SDL_GetPerformanceCounter();

        p1.last_input = p1.input;

        while(SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT) 
            {
                quit = 1;
                break;
            }
            else if (e.type == SDL_KEYDOWN && !e.key.repeat)
            {
                if (e.key.keysym.sym == SDLK_RETURN)
                {
                    p_reset_pos(&p1, W_WIDTH >> 2);
                    p_reset_pos(&p2, W_WIDTH - (W_WIDTH >> 2) - p2.r.w);
                }
            }
            p_input(&p1, e, PLAYER_ONE);
            //p_input(&p1, e, PLAYER_TWO);
        }
        
        Uint64 timer = SDL_GetTicks64();
        float delta = (timer - last_timer) / 1000.0f;

        if (p1.hit || p2.hit)
        {
            //freeze 
            if (++g_hit_timer < 2)
            {
                game_freeze = 1;
            }
            else 
            {
                game_freeze = 0;
            }
        }
        else g_hit_timer = 0;
        
        if (!game_freeze)
        {
            p_update(&p1, p2, delta);
            p_update(&p2, p1, delta);
        }

        last_timer = timer;

        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderClear(r);

        p_render(r, p1);
        p_render(r, p2);

        SDL_RenderPresent(r);

        Uint64 end = SDL_GetPerformanceCounter();
        float elapsed = (end - start) / (float)SDL_GetPerformanceFrequency() * 1000.0f;
        SDL_Delay(SDL_floor(FPS_CAP_MS - elapsed));
    }

    if (controllers != NULL)
    {
        g_close_controllers(controllers, num_joysticks);
        free(controllers);
    }

    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    IMG_Quit();
    SDL_Quit();

    return 0;
}

