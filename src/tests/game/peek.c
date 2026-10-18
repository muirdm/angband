/* game/peek.c
 *
 * Check that the "Peek into object creation" cheat option reports every
 * artifact placed on a level, and stays silent when switched off.
 */

#include "unit-test.h"
#include "unit-test-data.h"
#include "test-utils.h"

#include "cave.h"
#include "game-event.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "obj-pile.h"
#include "option.h"
#include "player.h"
#include "player-birth.h"
#include "player-util.h"
#include "z-util.h"

static int artifact_msgs;
static int other_peek_msgs;

static void event_message(game_event_type type, game_event_data *data,
		void *user)
{
	const char *m = data->message.msg;

	if (!m) return;
	if (prefix(m, "Artifact (")) {
		artifact_msgs++;
	} else if (prefix(m, "Ego-item (") || prefix(m, "Object (")) {
		other_peek_msgs++;
	}
}

int setup_tests(void **state)
{
	event_add_handler(EVENT_MESSAGE, event_message, NULL);
	set_file_paths();
	init_angband();
	if (!player_make_simple(NULL, NULL, "Tester")) return 1;
	return 0;
}

int teardown_tests(void *state)
{
	wipe_mon_list(cave, player);
	cleanup_angband();
	return 0;
}

/* Count artifacts lying on the floor or carried by monsters */
static int count_level_artifacts(struct chunk *c)
{
	int n = 0, y, x, i;

	for (y = 0; y < c->height; y++) {
		for (x = 0; x < c->width; x++) {
			struct object *obj = square_object(c, loc(x, y));

			for (; obj; obj = obj->next) {
				if (obj->artifact) n++;
			}
		}
	}
	for (i = 1; i < cave_monster_max(c); i++) {
		struct monster *mon = cave_monster(c, i);
		struct object *obj;

		if (!mon || !mon->race) continue;
		for (obj = mon->held_obj; obj; obj = obj->next) {
			if (obj->artifact) n++;
		}
	}
	return n;
}

static int make_levels(int count, int depth)
{
	int total = 0, i;

	for (i = 0; i < count; i++) {
		dungeon_change_level(player, depth);
		prepare_next_level(player);
		total += count_level_artifacts(cave);
	}
	return total;
}

static int test_peek_off(void *state)
{
	artifact_msgs = 0;
	other_peek_msgs = 0;
	option_set("cheat_peek", false);
	(void) make_levels(10, 40);
	eq(artifact_msgs, 0);
	eq(other_peek_msgs, 0);
	ok;
}

static int test_peek_on(void *state)
{
	int arts;

	artifact_msgs = 0;
	other_peek_msgs = 0;
	option_set("cheat_peek", true);
	/* Turning on a cheat option marks the score */
	require(OPT(player, score_peek));
	arts = make_levels(40, 40);
	require(arts > 0);
	eq(artifact_msgs, arts);
	/* Deep levels always have some ego items worth mentioning */
	require(other_peek_msgs > 0);
	ok;
}

const char *suite_name = "game/peek";
struct test tests[] = {
	{ "peek off", test_peek_off },
	{ "peek on", test_peek_on },
	{ NULL, NULL }
};
