#include "pebble_os.h"
#include "pebble_app.h"
#include "pebble_fonts.h"


#define MY_UUID { 0x8E, 0xFD, 0x77, 0xD9, 0x8C, 0xCB, 0x4B, 0x16, 0xA4, 0xAB, 0x6D, 0x84, 0xA2, 0xAD, 0xEA, 0xCE }
PBL_APP_INFO(MY_UUID,
             "BinaryBlob", "Jeff Lait",
             1, 0, /* App version */
             DEFAULT_MENU_ICON,
 //            APP_INFO_STANDARD_APP);
             APP_INFO_WATCH_FACE);

#include "mytypes.h"
#include "rand.h"

Window glbWindow;
Layer glbBlobLayer;

bool glbLive  = false;

#define WIDTH 144
#define HEIGHT 168
	
typedef struct 
{
	int	x, y;
} PT2;

#define FIXBITS 10

#define FIXMULT(a, b) (( (a)*(b) ) >> FIXBITS)
#define FIX2INT(a) ( ((a) + (1<<(FIXBITS-1))) >> FIXBITS)
#define INT2FIX(a) ((a) << FIXBITS)

void pt_add(PT2 *a, PT2 b)
{
	a->x += b.x;
	a->y += b.y;
}

void pt_sub(PT2 *a, PT2 b)
{
	a->x -= b.x;
	a->y -= b.y;
}

void pt_mul(PT2 *a, PT2 b)
{
	a->x = FIXMULT(a->x, b.x);
	a->y = FIXMULT(a->y, b.y);
}

int pt_normalize(PT2 *a)
{
	// There exists a norm in which this makes sense.
	int xdist = ABS(a->x);
	int ydist = ABS(a->y);
	if (!xdist && !ydist)
		return 0;
	
	if (xdist < ydist)
	{
		int scale = INT2FIX(1) / ydist;
		a->x = FIXMULT(a->x, scale);
		if (a->y < 0)
			a->y = INT2FIX(-1);
		else
			a->y = INT2FIX(1);
		return scale;
	}
	else
	{
		int scale = INT2FIX(1) / xdist;
		a->y = FIXMULT(a->y, scale);
		if (a->x < 0)
			a->x = INT2FIX(-1);
		else
			a->x = INT2FIX(1);
		return scale;
	}
}
	
typedef struct 
{
	PT2		pos;
	PT2		vel;
} PART;

#define NUM_PART 10
	
PART		glbPart[NUM_PART];

#define KERNEL_RAD 40
	
#define REFRESH_RATE 50
#define INTEGRATE_TIMER_ID 1
	
#define NUM_CLOCKBITS 10
	
#define CENTER_X 72
#define CENTER_Y 84
#define GRID_SPACING 10
	
int glbTargetMinute = -1;
	
const PT2 glbTargets[NUM_CLOCKBITS] =
{
	// Hour bits, high to low
	{ CENTER_X - 4.5 * GRID_SPACING, CENTER_Y - 4 * GRID_SPACING },
	{ CENTER_X - 1.5 * GRID_SPACING, CENTER_Y - 6 * GRID_SPACING },
	{ CENTER_X + 1.5 * GRID_SPACING, CENTER_Y - 3 * GRID_SPACING },
	{ CENTER_X + 4.5 * GRID_SPACING, CENTER_Y - 5 * GRID_SPACING },
		
	// Minute bits, high to low
	{ CENTER_X - 5.5 * GRID_SPACING, CENTER_Y + 3 * GRID_SPACING },
	{ CENTER_X - 2.5 * GRID_SPACING, CENTER_Y + 1 * GRID_SPACING },
	{ CENTER_X - 1.5 * GRID_SPACING, CENTER_Y + 5 * GRID_SPACING },
	{ CENTER_X + 1.5 * GRID_SPACING, CENTER_Y + 5 * GRID_SPACING },
	{ CENTER_X + 2.5 * GRID_SPACING, CENTER_Y + 1 * GRID_SPACING },
	{ CENTER_X + 5.5 * GRID_SPACING, CENTER_Y + 3 * GRID_SPACING },
};

PT2 glbPartTargets[NUM_PART];
	
const int glbBlinnKernel[KERNEL_RAD] =
{
	2048,
	2037,
	2002,
	1946,
	1872,
	1779,
	1673,
	1555,
	1429,
	1298,
	1167,
	1037,
	911,
	791,
	680,
	577,
	485,
	403,
	331,
	269,
	245,
	216,
	171,
	134,
	104,
	80,
	61,
	45,
	34,
	25,
	18,
	13,
	9,
	6,
	4,
	3,
	2,
	1,
	1,
	0,
};

int
blinn(int dist)
{
	dist = FIX2INT(dist);
	if (dist >= KERNEL_RAD)
		return 0;
	return glbBlinnKernel[dist];
}

int metadist(PT2 a, PT2 b)
{
	int adist = ABS(a.x - b.x);
	int bdist = ABS(a.y - b.y);
	return FIXMULT( blinn(adist), blinn(bdist) );
}

static int blob_plist[NUM_PART];
static int blob_plistx[NUM_PART];

void bloblayer_update(Layer *me, GContext *ctx)
{
	(void) me;
	
	// Useful for debugging that the timer shuts down
//	if (glbLive)
	if (1)
	{
		graphics_context_set_fill_color(ctx, GColorWhite);
		graphics_context_set_stroke_color(ctx, GColorBlack);
	}
	else
	{
		graphics_context_set_fill_color(ctx, GColorBlack);
		graphics_context_set_stroke_color(ctx, GColorWhite);
	}
	
	graphics_fill_rect(ctx, me->frame, 0, 0);
	
	for (int y = 0; y < HEIGHT; y++)
	{
		int nlive = 0;
		for (int part = 0; part < NUM_PART; part++)
		{
			int py = FIX2INT(glbPart[part].pos.y);
			py -= y;
			if (py < KERNEL_RAD && py > -KERNEL_RAD)
			{
				blob_plist[nlive++] = part;
			}
		}
		
		if (!nlive)
			continue;
		
		// Sort plist by x
		for (int i = 0; i < nlive-1; i++)
		{
			for (int j = i+1; j < nlive; j++)
			{
				if (glbPart[blob_plist[i]].pos.x > glbPart[blob_plist[j]].pos.x)
				{
					// Out of place
					int tmp = blob_plist[j];
					blob_plist[j] = blob_plist[i];
					blob_plist[i] = tmp;
				}
			}
		}
		
		for (int i = 0; i < nlive; i++)
			blob_plistx[i] = FIX2INT(glbPart[blob_plist[i]].pos.x);
		
		int sx = blob_plistx[0] - KERNEL_RAD;
		if (sx < 0)
			sx = 0;
		
		int startidx, endidx;
		startidx = 0;
		endidx = startidx;
		
		for (int x = sx; x < WIDTH; x++)
		{
			// Update our search range!
			while (endidx < nlive)
			{
				if (blob_plistx[endidx] - KERNEL_RAD> x)
					break;
				endidx++;
			}
			while (startidx < nlive)
			{
				if (blob_plistx[startidx] + KERNEL_RAD > x)
					break;
				startidx++;
			}
			
			PT2		cpos;
			cpos.x = INT2FIX(x);
			cpos.y = INT2FIX(y);
			int		totaldist = 0;
			for (int part = startidx; part < endidx; part++)
			{
				totaldist += metadist(glbPart[blob_plist[part]].pos, cpos);
			}
			if (totaldist > INT2FIX(1))
				graphics_draw_pixel(ctx, GPoint(x, y));
		}
	}
}

void handle_init(AppContextRef ctx) 
{
	(void)ctx;
	
	window_init(&glbWindow, "Main");
	window_stack_push(&glbWindow, true /* Animated */);
	window_set_background_color(&glbWindow, GColorWhite);
	
	resource_init_current_app(&APP_RESOURCES);

	layer_init(&glbBlobLayer, glbWindow.layer.frame);
	glbBlobLayer.update_proc = &bloblayer_update;
	layer_add_child(&glbWindow.layer, &glbBlobLayer);
	
	rand_seed();
	for (int part = 0; part < NUM_PART; part++)
	{
		glbPart[part].pos.x = rand_choice(INT2FIX(WIDTH));
		glbPart[part].pos.y = rand_choice(INT2FIX(HEIGHT));
		glbPart[part].vel.x = INT2FIX(rand_range(-3, 3));
		glbPart[part].vel.y = INT2FIX(rand_range(-3, 3));
	}
	
	glbLive = true;
	app_timer_send_event(ctx, REFRESH_RATE, INTEGRATE_TIMER_ID);
	
	layer_mark_dirty(&glbBlobLayer);
}

void
part_bounce(PART *part)
{
	if (part->pos.x < 0)
	{
		part->vel.x = ABS(part->vel.x);
		part->pos.x = -part->pos.x;
	}
	else if (part->pos.x > INT2FIX(WIDTH))
	{
		part->vel.x = -ABS(part->vel.x);
		part->pos.x = INT2FIX(2*WIDTH)-part->pos.x;
	}
	if (part->pos.y < 0)
	{
		part->vel.y = ABS(part->vel.y);
		part->pos.y = -part->pos.y;
	}
	else if (part->pos.y > INT2FIX(HEIGHT))
	{
		part->vel.y = -ABS(part->vel.y);
		part->pos.y = INT2FIX(2*HEIGHT)-part->pos.y;
	}
}

void
part_forces(PART *part, int pidx)
{
	PT2 targetforce;
	targetforce = glbPartTargets[pidx];
	targetforce.x = INT2FIX(targetforce.x);
	targetforce.y = INT2FIX(targetforce.y);
	pt_sub(&targetforce, part->pos);
	int			len;
	len = MAX(ABS(targetforce.x), ABS(targetforce.y));
	pt_normalize(&targetforce);
	if (len < INT2FIX(4))
	{
		targetforce.x >>= 2;
		targetforce.y >>= 2;
	}
	
	pt_add(&part->vel, targetforce);
	
	PT2 dragforce;
	dragforce.x = 0.8 * (INT2FIX(1));
	dragforce.y = 0.8 * (INT2FIX(1));
	pt_mul(&part->vel, dragforce);
}

bool
part_integrate()
{
	// Integrate!
	for (int pidx= 0; pidx < NUM_PART; pidx++)
	{
		PART *part = &glbPart[pidx];
		pt_add(&part->pos, part->vel);
		
		part_bounce(part);
		
		part_forces(part, pidx);
		
//		part->pos.x = INT2FIX(glbPartTargets[pidx].x);
//		part->pos.y = INT2FIX(glbPartTargets[pidx].y);
	}
	
	// Check to see if we are on target!
	for (int pidx = 0; pidx < NUM_PART; pidx++)
	{
		if (FIX2INT(glbPart[pidx].pos.x) != glbPartTargets[pidx].x)
			return true;
		if (FIX2INT(glbPart[pidx].pos.y) != glbPartTargets[pidx].y)
			return true;
	}
	
	return false;
}

void
handle_tick(AppContextRef ctx, PebbleTickEvent *event)
{
	// Set our targets.
	PblTm	time;
	
	get_time(&time);
	
	if (time.tm_min == glbTargetMinute)
		return;
	
	glbTargetMinute = time.tm_min;
	
	int 	ntarget = 0;
	static int		targets[NUM_CLOCKBITS];
	
	if (time.tm_hour > 12)
		time.tm_hour -= 12;
	
	for (int bit = 0; bit < 4; bit++)
	{
		if (time.tm_hour & (1 << (3-bit)))
			targets[ntarget++] = bit;
	}
	for (int bit = 0; bit < 6; bit++)
	{
		if (time.tm_min & (1 << (5-bit)))
			targets[ntarget++] = bit + 4;
	}
	
	// Assign targets.
	// If no targets, it is midnight/noon, scatter!
	if (!ntarget)
	{
		for (int part = 0; part < NUM_PART; part++)
		{
			glbPartTargets[part].x = rand_choice((WIDTH));
			glbPartTargets[part].y = rand_choice((HEIGHT));
		}
	}
	else
	{
		// First assign each target to one particles.
		static int partlist[NUM_PART];
		for (int part = 0; part < NUM_PART; part++)
			partlist[part] = part;
		
		for (int i = 0; i < ntarget; i++)
		{
			// Find a random particle..
			int pidx = rand_choice(NUM_PART - i);
			
			// Swap...
			int tmp = partlist[pidx+i];
			partlist[pidx+i] = partlist[i];
			partlist[i] = tmp;
			
			glbPartTargets[partlist[i]] = glbTargets[targets[i]];
		}
		// Remaining particles get a random target.
		for (int i = ntarget; i < NUM_PART; i++)
		{
			int t = rand_choice(ntarget);
			glbPartTargets[partlist[i]] = glbTargets[targets[t]];
		}
	}
	
	if (!glbLive)
	{
		glbLive = true;
		app_timer_send_event(ctx, REFRESH_RATE, INTEGRATE_TIMER_ID);
	}
	
	layer_mark_dirty(&glbBlobLayer);
}

void
handle_timer(AppContextRef ctx, AppTimerHandle handle, uint32_t cookie)
{
	if (cookie == INTEGRATE_TIMER_ID)
	{
		glbLive = part_integrate();
		
		if (glbLive)
			app_timer_send_event(ctx, REFRESH_RATE, INTEGRATE_TIMER_ID);
		layer_mark_dirty(&glbBlobLayer);
	}
}


void pbl_main(void *params) 
{
	PebbleAppHandlers handlers = 
	{
		.init_handler = &handle_init,
		.tick_info =
		{
			.tick_handler = &handle_tick,
			.tick_units = SECOND_UNIT
		},
		.timer_handler = &handle_timer
	};
	
	app_event_loop(params, &handlers);
}
