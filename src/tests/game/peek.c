/* game/peek.c
 *
 * Check that the "Peek into object creation" cheat option reports every
 * artifact placed on a level, and stays silent when switched off.
 */

#include <ctype.h>
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
#include "obj-tval.h"
#include "obj-util.h"
#include "obj-pile.h"
#include "option.h"
#include "player.h"
#include "player-birth.h"
#include "player-util.h"
#include "z-util.h"

static int artifact_msgs;
static int other_peek_msgs;
/* Total number of items in "Ego-item" and "Object" mentions */
static int other_peek_items;
/* Whether the same mention was given twice since last reset */
static bool repeated_mention;
/* Whether a non-artifact mention named its carrier */
static bool carrier_named;
static int monster_msgs;
static char *mentions[1000];
static int n_mentions;

static void reset_mentions(void)
{
	int i;

	for (i = 0; i < n_mentions; i++) string_free(mentions[i]);
	n_mentions = 0;
	repeated_mention = false;
	carrier_named = false;
	monster_msgs = 0;
	artifact_msgs = 0;
	other_peek_msgs = 0;
	other_peek_items = 0;
}

static void note_mention(const char *m)
{
	const char *x = strstr(m, " x");
	int i, n = 1;

	if (x && isdigit((unsigned char) x[2])) n = atoi(x + 2);
	if (prefix(m, "Ego-item (") || prefix(m, "Object (")) {
		other_peek_items += n;
		if (strstr(m, "carried by")) carrier_named = true;
	}
	for (i = 0; i < n_mentions; i++) {
		if (streq(mentions[i], m)) repeated_mention = true;
	}
	if (n_mentions < (int) N_ELEMENTS(mentions)) {
		mentions[n_mentions++] = string_make(m);
	}
}

static void event_message(game_event_type type, game_event_data *data,
		void *user)
{
	const char *m = data->message.msg;

	if (!m) return;
	if (prefix(m, "Artifact (")) {
		artifact_msgs++;
	} else if (prefix(m, "Ego-item (") || prefix(m, "Object (")) {
		other_peek_msgs++;
		note_mention(m);
	} else if (prefix(m, "Deep monster (") || prefix(m, "Deep unique (")
			|| prefix(m, "Unique (")) {
		monster_msgs++;
		note_mention(m);
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

/* Count non-artifact, non-money objects the filter lets through */
static int count_level_shown(struct chunk *c)
{
	int n = 0, y, x, i;

	for (y = 0; y < c->height; y++) {
		for (x = 0; x < c->width; x++) {
			struct object *obj = square_object(c, loc(x, y));

			for (; obj; obj = obj->next) {
				/* Mimics are monsters, not items */
				if (obj->mimicking_m_idx) continue;
				if (!obj->artifact && !tval_is_money(obj) &&
						!object_ignore_peek(obj)) n += obj->number;
			}
		}
	}
	for (i = 1; i < cave_monster_max(c); i++) {
		struct monster *mon = cave_monster(c, i);
		struct object *obj;

		if (!mon || !mon->race) continue;
		for (obj = mon->held_obj; obj; obj = obj->next) {
			if (!obj->artifact && !tval_is_money(obj) &&
					!object_ignore_peek(obj)) n += obj->number;
		}
	}
	return n;
}

static int test_peek_filter_shows_all(void *state)
{
	int shown = 0, i;

	/* Any filter setting switches on mentioning everything it lets by */
	require(!ignore_filter_active());
	ignore_level[ITYPE_SHARP] = IGNORE_ALL;
	require(ignore_filter_active());

	option_set("cheat_peek", true);
	for (i = 0; i < 10; i++) {
		int here;

		reset_mentions();
		dungeon_change_level(player, 40);
		prepare_next_level(player);
		here = count_level_shown(cave);
		/* Every item still on the level was counted when it was made */
		require(other_peek_items >= here);
		/* Like items are given once, with their total */
		require(!repeated_mention);
		require(!carrier_named);
		shown += here;
	}
	require(shown > 0);
	reset_mentions();

	/* Toggling ignoring off switches the filter off */
	player->unignoring = 1;
	require(!ignore_filter_active());
	player->unignoring = 0;

	ignore_level[ITYPE_SHARP] = IGNORE_NONE;
	option_set("cheat_peek", false);
	ok;
}

static int test_hear_combines(void *state)
{
	int i, msgs = 0;

	option_set("cheat_hear", true);
	for (i = 0; i < 10; i++) {
		reset_mentions();
		dungeon_change_level(player, 30);
		prepare_next_level(player);
		/* Each kind of deep monster is given once, with its total */
		require(!repeated_mention);
		msgs += monster_msgs;
	}
	require(msgs > 0);
	reset_mentions();
	option_set("cheat_hear", false);
	ok;
}

const char *suite_name = "game/peek";
struct test tests[] = {
	{ "peek off", test_peek_off },
	{ "peek on", test_peek_on },
	{ "peek ignore", test_peek_ignore },
	{ "peek respects ignore", test_peek_respects_ignore },
	{ "peek filter shows all", test_peek_filter_shows_all },
	{ "hear combines", test_hear_combines },
	{ NULL, NULL }
};
