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
#include "obj-ignore.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-util.h"
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


/* Make a plain floor long sword the player has seen but not walked over */
static struct object *seen_sword(void)
{
	struct object_kind *kind = lookup_kind(TV_SWORD,
		lookup_sval(TV_SWORD, "Long Sword"));
	struct object *obj = object_new();

	object_prep(obj, kind, 0, MINIMISE);
	/* Magical, so its bonuses aren't known until walked over */
	obj->to_h = 5;
	obj->to_d = 5;
	obj->known = object_new();
	object_set_base_known(player, obj);
	return obj;
}

static void free_sword(struct object *obj)
{
	object_free(obj->known);
	object_free(obj);
}

static int test_peek_ignore(void *state)
{
	struct object *obj = seen_sword();
	struct ego_item *ego = NULL;
	int i;

	for (i = 0; i < z_info->e_max; i++) {
		if (e_info[i].name && streq(e_info[i].name, "of Westernesse")) {
			ego = &e_info[i];
			break;
		}
	}
	require(ego);

	/* Hide every non-artifact sword */
	ignore_level[ITYPE_SHARP] = IGNORE_ALL;
	option_set("cheat_peek", false);
	require(!object_is_ignored(obj));
	option_set("cheat_peek", true);
	require(object_is_ignored(obj));

	/* "Good" keeps egos, so an unwalked ego sword stays visible... */
	ignore_level[ITYPE_SHARP] = IGNORE_GOOD;
	obj->ego = ego;
	require(!object_is_ignored(obj));
	/* ...unless that ego is ignored */
	ego_ignore_toggle(ego->eidx, ITYPE_SHARP);
	require(object_is_ignored(obj));
	option_set("cheat_peek", false);
	require(!object_is_ignored(obj));
	ego_ignore_toggle(ego->eidx, ITYPE_SHARP);

	/* Artifacts are never hidden by quality */
	option_set("cheat_peek", true);
	ignore_level[ITYPE_SHARP] = IGNORE_ALL;
	obj->ego = NULL;
	obj->artifact = &a_info[1];
	require(!object_is_ignored(obj));
	obj->artifact = NULL;

	ignore_level[ITYPE_SHARP] = IGNORE_NONE;
	option_set("cheat_peek", false);
	free_sword(obj);
	ok;
}

static int test_peek_respects_ignore(void *state)
{
	int arts, i;

	/* Ignore every non-artifact object */
	for (i = 0; i < ITYPE_MAX; i++) ignore_level[i] = IGNORE_ALL;
	for (i = 0; i < z_info->k_max; i++) {
		if (!k_info[i].name) continue;
		kind_ignore_when_aware(&k_info[i]);
		kind_ignore_when_unaware(&k_info[i]);
	}

	artifact_msgs = 0;
	other_peek_msgs = 0;
	option_set("cheat_peek", true);
	arts = make_levels(40, 40);
	require(arts > 0);
	/* Artifacts are still announced, everything else is hidden */
	eq(artifact_msgs, arts);
	eq(other_peek_msgs, 0);

	for (i = 0; i < ITYPE_MAX; i++) ignore_level[i] = IGNORE_NONE;
	for (i = 0; i < z_info->k_max; i++) kind_ignore_clear(&k_info[i]);
	option_set("cheat_peek", false);
	ok;
}

const char *suite_name = "game/peek";
struct test tests[] = {
	{ "peek off", test_peek_off },
	{ "peek on", test_peek_on },
	{ "peek ignore", test_peek_ignore },
	{ "peek respects ignore", test_peek_respects_ignore },
	{ NULL, NULL }
};
