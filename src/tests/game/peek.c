/* game/peek.c
 *
 * Check the "Peek into object creation" (cheat_peek) and "Peek into monster
 * creation" (cheat_hear) cheat options: every artifact placed on a level is
 * reported, other reports are limited to potions of Healing, *Healing* and
 * Life, Power Dragon Scale Mail and unique monsters, and like reports are
 * combined into one per level.  Also check that with cheat_peek on, ignore
 * settings hide floor items before they are walked over.
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
#include "monster.h"
#include "obj-ignore.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "option.h"
#include "player.h"
#include "player-birth.h"
#include "player-util.h"
#include "z-util.h"

static int artifact_msgs;
static int object_msgs;
static int object_items;
static int unique_msgs;
/* A report of something not on the list */
static bool unwanted_mention;
/* The same report given twice for one level */
static bool repeated_mention;
static char *mentions[1000];
static int n_mentions;

static const char *wanted_objects[] = {
	"Potion of Healing", "Potion of *Healing*", "Potion of Life",
	"Power Dragon Scale Mail"
};

static void reset_mentions(void)
{
	int i;

	for (i = 0; i < n_mentions; i++) string_free(mentions[i]);
	n_mentions = 0;
	artifact_msgs = 0;
	object_msgs = 0;
	object_items = 0;
	unique_msgs = 0;
	unwanted_mention = false;
	repeated_mention = false;
}

static void note_repeat(const char *m)
{
	int i;

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
		const char *name = strchr(m, '(') + 1;
		const char *x = strstr(m, " x");
		bool wanted = false;
		size_t i;

		for (i = 0; i < N_ELEMENTS(wanted_objects); i++) {
			const char *w = wanted_objects[i];

			/* The name, then the end or a count */
			if (prefix(name, w) && (name[strlen(w)] == ')'
					|| name[strlen(w)] == ' ')) wanted = true;
		}
		/* Only non-potions say who carries them */
		if (!wanted || (strstr(m, "carried by") && prefix(name, "Potion")))
			unwanted_mention = true;
		object_msgs++;
		object_items += (x && isdigit((unsigned char) x[2])) ?
			atoi(x + 2) : 1;
		note_repeat(m);
	} else if (prefix(m, "Unique (") || prefix(m, "Deep unique (")) {
		unique_msgs++;
		note_repeat(m);
	} else if (prefix(m, "Deep monster (")) {
		unwanted_mention = true;
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
	reset_mentions();
	wipe_mon_list(cave, player);
	cleanup_angband();
	return 0;
}

static bool object_is_wanted(const struct object *obj)
{
	if (obj->artifact) return false;
	if (obj->tval == TV_POTION) {
		return obj->sval == lookup_sval(TV_POTION, "Healing")
			|| obj->sval == lookup_sval(TV_POTION, "*Healing*")
			|| obj->sval == lookup_sval(TV_POTION, "Life");
	}
	return obj->tval == TV_DRAG_ARMOR && obj->sval ==
		lookup_sval(TV_DRAG_ARMOR, "Power Dragon Scale Mail");
}

/* Count artifacts, and wanted non-artifact items, on a level */
static void count_level(struct chunk *c, int *arts, int *wanted)
{
	int y, x, i;

	for (y = 0; y < c->height; y++) {
		for (x = 0; x < c->width; x++) {
			struct object *obj = square_object(c, loc(x, y));

			for (; obj; obj = obj->next) {
				/* Mimics are monsters, not items */
				if (obj->mimicking_m_idx) continue;
				if (obj->artifact) (*arts)++;
				else if (object_is_wanted(obj)) *wanted += obj->number;
			}
		}
	}
	for (i = 1; i < cave_monster_max(c); i++) {
		struct monster *mon = cave_monster(c, i);
		struct object *obj;

		if (!mon || !mon->race) continue;
		for (obj = mon->held_obj; obj; obj = obj->next) {
			if (obj->artifact) (*arts)++;
			else if (object_is_wanted(obj)) *wanted += obj->number;
		}
	}
}

static void make_level(int depth)
{
	reset_mentions();
	dungeon_change_level(player, depth);
	prepare_next_level(player);
}

static int test_peek_off(void *state)
{
	int i;

	option_set("cheat_peek", false);
	option_set("cheat_hear", false);
	for (i = 0; i < 10; i++) {
		make_level(40);
		eq(artifact_msgs, 0);
		eq(object_msgs, 0);
		eq(unique_msgs, 0);
	}
	ok;
}

static int test_peek_on(void *state)
{
	int i, arts_total = 0, wanted_total = 0;

	option_set("cheat_peek", true);
	/* Turning on a cheat option marks the score */
	require(OPT(player, score_peek));
	for (i = 0; i < 40; i++) {
		int arts = 0, wanted = 0;

		make_level(50);
		count_level(cave, &arts, &wanted);
		/* Every artifact is reported */
		eq(artifact_msgs, arts);
		/* Exactly the wanted items on the level are reported */
		eq(object_items, wanted);
		require(!unwanted_mention);
		require(!repeated_mention);
		arts_total += arts;
		wanted_total += wanted;
	}
	require(arts_total > 0);
	require(wanted_total > 0);
	option_set("cheat_peek", false);
	ok;
}

static int test_hear_uniques_only(void *state)
{
	int i, total = 0;

	option_set("cheat_hear", true);
	for (i = 0; i < 10; i++) {
		int j, uniques = 0;

		make_level(30);
		/* Only uniques, each once */
		require(!unwanted_mention);
		require(!repeated_mention);
		/* Exactly the uniques on the level are reported */
		for (j = 1; j < cave_monster_max(cave); j++) {
			struct monster *mon = cave_monster(cave, j);

			if (mon && mon->race && rf_has(mon->race->flags, RF_UNIQUE))
				uniques++;
		}
		eq(unique_msgs, uniques);
		total += unique_msgs;
	}
	require(total > 0);
	option_set("cheat_hear", false);
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

const char *suite_name = "game/peek";
struct test tests[] = {
	{ "peek off", test_peek_off },
	{ "peek on", test_peek_on },
	{ "peek ignore", test_peek_ignore },
	{ "hear uniques only", test_hear_uniques_only },
	{ NULL, NULL }
};
