/*
 * nsnake.c -- a snake game for your terminal
 *
 * Copyright (c) 2011, David Demelier <markand@malikania.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

// #define _GNU_SOURCE
// #define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <err.h>
#include <signal.h>
#include <unistd.h>
#include <curses.h>
#include <time.h>
#include <pwd.h>

#ifndef SCOREFILE
#define SCOREFILE	"/var/db/nsnake/scores"
#endif

#define	HEIGHT		23
#define WIDTH		78
#define SIZE		((HEIGHT - 2) * (WIDTH - 2))

#define EMPTY		0
#define WALL		1
#define SNAKE		2
#define FOOD		3

#define WSET(frame, pair)	if (setcolors) wattron(frame, pair)
#define WUNSET(frame, pair)	if (setcolors) wattroff(frame, pair)
#define REFRESH()		refresh(); wrefresh(top); wrefresh(frame);

/* --------------------------------------------------------
 * structure definitions
 * -------------------------------------------------------- */

struct snake {
	uint32_t	score;		/* user score */
	uint16_t	length;		/* snake's size */
	int8_t		dirx;		/* direction in x could be 0, 1 or -1 */
	int8_t		diry;		/* same for y */

	struct {
		uint8_t x;		/* each snake part has (x, y) position */
		uint8_t y;		/* each part will be displayed */
	} pos[SIZE];
};

struct food {
	enum {
		NORM = 0,		/* both increase the score but only NORM */
		FREE			/* increase the snake's length too */
	} type;

	uint8_t x;			/* Position of the current food, will be used */
	uint8_t y;			/* in grid[][]. */
};

struct score {
	char		name[32];	/* highscore's name */
	uint32_t	score;		/* score */
	time_t		time;		/* when? */
	uint8_t		wc;		/* wallcrossing or not */
};

/* --------------------------------------------------------
 * global variables and options
 * -------------------------------------------------------- */

static int	setcolors	= 1;	/* enable colors */
static int	warp		= 1;	/* enable wall crossing */
static int	color		= 2;	/* green color by default */
static int	noscore		= 0;	/* do not score */

static uint8_t	grid[HEIGHT][WIDTH] = { { EMPTY } };
static WINDOW	*top = NULL;
static WINDOW	*frame = NULL;

/* --------------------------------------------------------
 * prototypes
 * -------------------------------------------------------- */

static bool	uinit(void);
static void	setgrid(struct snake *);
static void	draw(const struct snake *, const struct food *);
static bool	isdead(const struct snake *);
static bool	iseaten(const struct snake *);
static void	spawn(struct food *);
static void	direction(struct snake *, int);
static bool	registerscore(const struct snake *);
static bool	appendscore(const struct score *);
static bool	insertscore(const struct score *);
static void	showscores(void);
static void	resizehandler(int);
static void	quit(const struct snake *);
static void	usage(void);

int
main(int argc, char *argv[])
{
	int running;
	int x, y, ch;

	struct snake sn = {
		.score = 0,	.length = 4,
		.dirx = 1,	.diry = 0,
		.pos = { {5, 10}, {5, 9}, {5, 8}, {5, 7} }
	};

	struct food fd = { .type = NORM };
	struct timespec delay = { .tv_sec = 0, .tv_nsec = 100000000L };

	/* Process options */
	while ((ch = getopt(argc, argv, "cC:nsw")) != -1) {
		switch (ch) {
		case 'c': setcolors = 0;	break;
		case 'C': color = atoi(optarg);	break;
		case 'n': noscore = 1;		break;
		case 's': showscores();		break;
		case 'w': warp = 0;		break;
		case '?':
		default :
			  usage();
			  /* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	/* Game init */
	srandom(time(NULL));
	signal(SIGWINCH, resizehandler);

	if (!uinit()) {
		quit(NULL);
		errx(1, "Terminal too small, aborting");
	}
		
	if (top == NULL || frame == NULL) {
		endwin();
		errx(1, "ncurses failed to init");
	}

	/* Apply WALL to the edges */
	for (y = 0; y < HEIGHT; ++y)
		grid[y][0] = grid[y][WIDTH - 1] = WALL;
	for (x = 0; x < WIDTH; ++x)
		grid[0][x] = grid[HEIGHT - 1][x] = WALL;

	/* Do not spawn food on snake */
	setgrid(&sn);
	spawn(&fd);

	/* Apply food on the grid */
	grid[fd.y][fd.x] = FOOD;
	draw(&sn, &fd);

	/* First direction is to right */
	sn.pos[0].x += sn.dirx;

	running = 1;
	while (!isdead(&sn) && running) {
		if (iseaten(&sn)) {
			int i;

			if (fd.type == NORM)
				sn.length += 2;

			for (i = 0; i < sn.length; ++i)
				grid[sn.pos[i].y][sn.pos[i].x] = SNAKE;

			/* If the screen is totally filled */
			if (sn.length >= SIZE) {
				/* Emulate new game */
				for (i = 4; i < SIZE; ++i) {
					(void)mvwaddch(frame, sn.pos[i].y, sn.pos[i].x, ' ');
					grid[sn.pos[i].y][sn.pos[i].x] = EMPTY;
				}

				sn.length = 4;
			}

			if (fd.type == NORM)
				setgrid(&sn);

			/* Prevent food spawning on snake's tail */
			spawn(&fd);

			sn.score += 1;
			grid[fd.y][fd.x] = FOOD;
		}

		/* Draw and define grid with snake parts */
		draw(&sn, &fd);
		setgrid(&sn);

		/* Go to the next position */
		sn.pos[0].x += sn.dirx;
		sn.pos[0].y += sn.diry;

		ch = getch();
		if (ch == 'p') {
			nodelay(stdscr, FALSE);
			while ((ch = getch()) != 'p' && ch != 'q')
				;

			if (ch == 'q')
				running = 0;

			nodelay(stdscr, TRUE);
		} else if (ch == 'q')
			running = false;
		else if (ch == 'c')
			color = (color + 1) % 8;
		else if (ch)
			direction(&sn, ch);

		/* If warp enabled, touching wall cross to the opposite */
		if (warp) {
			if (sn.pos[0].x == WIDTH - 1)
				sn.pos[0].x = 1;
			else if (sn.pos[0].x == 0)
				sn.pos[0].x = WIDTH - 2;
			else if (sn.pos[0].y == HEIGHT - 1)
				sn.pos[0].y = 1;
			else if (sn.pos[0].y == 0)
				sn.pos[0].y = HEIGHT - 2;
		}

		nanosleep(&delay, NULL);
	}

	/* The snake is dead. */
	quit(&sn);

	/* User has left or is he dead? */
	printf("%sScore: %d\n", (grid[sn.pos[0].y][sn.pos[0].x] != EMPTY) ?
	    "You died...\n" : "", sn.score);

	if (!noscore && !registerscore(&sn))
		err(1, "Could not write score file %s", SCOREFILE);
	
	return 0;
}

static bool
uinit(void)
{
	/* Ncurses init */
	initscr(); noecho(); curs_set(0);
	keypad(stdscr, TRUE); nodelay(stdscr, TRUE);

	if (COLS < (WIDTH + 1) || LINES < (HEIGHT + 1))	
		return false;

	if (color < 0 || color > 8)
		color = 4;
	else
		color += 2;

	if (setcolors && has_colors()) {
		int i;

		use_default_colors();
		start_color();

		init_pair(0, COLOR_WHITE, COLOR_BLACK);		/* topbar */
		init_pair(1, COLOR_YELLOW, -1);			/* food */

		for (i = 0; i < COLORS; ++i)
			init_pair(i + 2, i, -1);
	}

	top	= newwin(1, 0, 0, 0);
	frame	= newwin(HEIGHT, WIDTH, (LINES/2)-(HEIGHT/2), (COLS/2)-(WIDTH/2));
	box(frame, ACS_VLINE, ACS_HLINE);

	if (setcolors) {
		wbkgd(top, COLOR_PAIR(0));
		wattrset(top, COLOR_PAIR(0) | A_BOLD);
	}

	REFRESH();

	return true;
}

static void
setgrid(struct snake *sn)
{
	uint16_t i;

	for (i = 0; i < sn->length; ++i)
		grid[sn->pos[i].y][sn->pos[i].x] = SNAKE;

	/*
	 * each snake part must follow the last part, pos[0] is head, then
	 * pos[2] takes pos[1] place, pos[3] takes pos[2] and so on.
	 */
	grid[sn->pos[sn->length-1].y][sn->pos[sn->length-1].x] = EMPTY;
	memmove(&sn->pos[1], &sn->pos[0], sizeof (sn->pos));
}

static void
draw(const struct snake *sn, const struct food *fd)
{
	uint16_t i;

	for (i = 0; i < sn->length; ++i) {
		WSET(frame, COLOR_PAIR(color));
		(void)mvwaddch(frame, sn->pos[i].y, sn->pos[i].x, '#');
		WUNSET(frame, COLOR_PAIR(color));
	}

	/* Print head */
	WSET(frame, COLOR_PAIR(color) | A_BOLD);
	(void)mvwaddch(frame, sn->pos[0].y, sn->pos[0].x, '@');
	WUNSET(frame, COLOR_PAIR(color) | A_BOLD);

	/* Erase the snake's tail */
	(void)mvwaddch(frame, sn->pos[sn->length].y, sn->pos[sn->length].x, ' ');

	/* Print food */
	WSET(frame, COLOR_PAIR(1) | A_BOLD);
	(void)mvwaddch(frame, fd->y, fd->x, (fd->type == FREE) ? '*' : '+');
	WUNSET(frame, COLOR_PAIR(1) | A_BOLD);

	/* Print score */
	(void)wmove(top, 0, 0);
	(void)wprintw(top, "Score : %d", sn->score);

	REFRESH();
}

static bool
isdead(const struct snake *sn)
{
	if (grid[sn->pos[0].y][sn->pos[0].x] == SNAKE)
		return true;

	/* No warp enabled means dead in wall */
	return (!warp && grid[sn->pos[0].y][sn->pos[0].x] == WALL);
}

static bool
iseaten(const struct snake *sn)
{
	return grid[sn->pos[0].y][sn->pos[0].x] == FOOD;
}

static void
spawn(struct food *fd)
{
	int num;

	do {
		fd->x = (random() % (WIDTH - 2)) + 1;
		fd->y = (random() % (HEIGHT - 2)) + 1;
	} while (grid[fd->y][fd->x] != EMPTY);

	/* Free food does not grow the snake */
	num		= ((random() % 7) - 1) + 1;
	fd->type	= (num == 6) ? FREE : NORM;
}

static void
direction(struct snake *sn, int ch)
{
	switch (ch) {
	case KEY_LEFT: case 'h': case 'H':
		if (sn->dirx != 1) { sn->dirx = -1; sn->diry = 0; }
		break;
	case KEY_UP: case 'k': case 'K':
		if (sn->diry != 1) { sn->dirx = 0; sn->diry = -1; }
		break;
	case KEY_DOWN: case 'j': case 'J':
		if (sn->diry != -1) { sn->dirx = 0; sn->diry = 1; }
		break;
	case KEY_RIGHT: case 'l': case 'L':
		if (sn->dirx != -1) { sn->dirx = 1; sn->diry = 0; }
	default:
		break;
	}
}

static bool
registerscore(const struct snake *sn)
{
	struct score sc;
	bool (*reshandler)(const struct score *);

	bzero(&sc, sizeof (sc));
	strncpy(sc.name, getpwuid(getuid())->pw_name, sizeof (sc.name));
	sc.score	= sn->score;
	sc.time		= time(NULL);
	sc.wc		= warp;

	reshandler = (access(SCOREFILE, F_OK) == -1) ? &appendscore : &insertscore;

	return reshandler(&sc);
}

/*
 * Create the file and append the score when it does not exists.
 */

static bool
appendscore(const struct score *sc)
{
	FILE *fp;
	char header[12] = "nsnake-score";
	uint32_t nscore = 1;

	if (!(fp = fopen(SCOREFILE, "w+b")))
		return false;

	fwrite(header, sizeof (header), 1, fp);
	fwrite(&nscore, sizeof (nscore), 1, fp);
	fwrite(sc, sizeof (*sc), 1, fp);

	fclose(fp);

	return true;
}

static bool
insertscore(const struct score *sc)
{
	FILE *fp;
	uint32_t nscore, i;
	char header[12] = { 0 };
	struct score *buffer;

	if (!(fp = fopen(SCOREFILE, "r+b")))
		return false;

	fread(header, sizeof (header), 1, fp);
	if (strncmp(header, "nsnake-score", sizeof (header)) != 0) {
		fclose(fp);
		return false;
	}

	fread(&nscore, sizeof (nscore), 1, fp);
	if (!(buffer = calloc(nscore + 1, sizeof (*buffer)))) {
		fclose(fp);
		return false;
	}

	fread(buffer, sizeof (*buffer), nscore, fp);
	for (i = 0; i < nscore; ++i)
		if (sc->score >= buffer[i].score && sc->wc == buffer[i].wc)
			break;

	/* Replace same score */
	if (sc->score == buffer[i].score && strcmp(buffer[i].name, sc->name) == 0)
		memcpy(&buffer[i], sc, sizeof (*sc));
	else {
		memmove(&buffer[i + 1], &buffer[i], (sizeof (*sc)) * (nscore - i));
		memcpy(&buffer[i], sc, sizeof (*sc));

		/* There is now a new entry */
		++ nscore;

		/* Update number of score entries */
		fseek(fp, sizeof (header), SEEK_SET);
		fwrite(&nscore, sizeof (nscore), 1, fp);
	}
		
	/* Finally write */
	fseek(fp, sizeof (header) + sizeof (nscore), SEEK_SET);
	fwrite(buffer, sizeof (*sc), nscore, fp);

	free(buffer);
	fclose (fp);

	return true;
}

static void
showscores(void)
{
	FILE *fp;
	uint32_t nscore, i;
	char header[12] = { 0 };
	struct score sc;

	if (!(fp = fopen(SCOREFILE, "rb")))
		err(1, "Could not read %s", SCOREFILE);

	printf("Wall crossing %s\n", (warp) ? "enabled" : "disabled");

	fread(header, sizeof (header), 1, fp);
	if (strncmp(header, "nsnake-score", sizeof (header)) != 0)
		errx(1, "Not a valid nsnake score file");

	fread(&nscore, sizeof (nscore), 1, fp);
	for (i = 0; i < nscore; ++i) {
		fread(&sc, sizeof (sc), 1, fp);

		if (sc.wc == warp) {
			char date[128] = { 0 };
			struct tm *tm = localtime(&sc.time);

			strftime(date, sizeof (date), "%F %H:%M", tm);
			printf("%s\t\t%u\t\t%s\n", sc.name, sc.score, date);
		}
	}

	fclose(fp);
	exit(0);
}

void
resizehandler(int signal)
{
	int x, y;

	if (signal != SIGWINCH)
		return;

	/* XXX: I would like to pause the game until the terminal is resized */
	endwin(); resizeterm(LINES, COLS);
	refresh(); clear();

	/* Color the top bar */
	wbkgd(top, COLOR_PAIR(0) | A_BOLD);

	getmaxyx(stdscr, y, x);

	if (x < WIDTH || y < HEIGHT) {
		quit(NULL);
		errx(1, "Terminal has been resized too small, aborting");
	}

	mvwin(frame, (y / 2) - (HEIGHT / 2), (x / 2) - (WIDTH / 2));

	REFRESH();
}

static void
quit(const struct snake *sn)
{
	struct timespec delay = { .tv_sec = 0, .tv_nsec = 50000000 };
	uint16_t i;

	if (sn != NULL) {
		for (i = 0; i < sn->length; ++i) {
			(void)mvwaddch(frame, sn->pos[i].y, sn->pos[i].x, ' ');
			nanosleep(&delay, NULL);
			REFRESH();
		}
	}

	delwin(top); delwin(frame); delwin(stdscr);
	endwin();
}

static void
usage(void)
{
	fprintf(stderr, "usage: nsnake [-cnsw] [-C color]\n");
	exit(1);
}
