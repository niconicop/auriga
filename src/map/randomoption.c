/*
 * Copyright (C) 2002-2007  Auriga
 *
 * This file is part of Auriga.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>


#include "db.h"
#include "nullpo.h"
#include "malloc.h"
#include "utils.h"

#include "randomoption.h"
#include "clif.h"
#include "map.h"
#include "pc.h"
#include "mob.h"
#include "script.h"
#include "unit.h"
#include "itemdb.h"
#include "timer.h"
#include "party.h"
#include "status.h"
#include "battle.h"
#include "bonus.h"

 //Item Random Option System [Cyrus]
#define MAX_RANDOMOPTION 256
#define MAX_RANDOMOPTION_ENTRY 32
#define MAX_RANDOMOPTION_GROUP 16
#define MAX_RANDOMOPTION_DROP MOB_ID_MAX - MOB_ID_MIN
#define MAX_ITEM_RANDOMOPTION 5
#define MAX_ITEM_RANDOMOPTION_DROP 8
#define MAX_ITEM_RANDOMOPTION_RATE 10000

#define RANDOMOPTION_BONUS1 bonus_param1
#define RANDOMOPTION_BONUS2 bonus_param2
#define RANDOMOPTION_BONUS3 bonus_param3
#define RANDOMOPTION_BONUS4 bonus_param4
#define RANDOMOPTION_BONUS5 //

int randomoption_cruuent_opt;
int randomoption_current_val;

struct randomoption_group_optoin_entry {
	int option;
	int value_min;
	int value_max;
	int qty;
	int rate;
};

struct randomoption_group_optoin {
	struct randomoption_group_optoin_entry option[MAX_RANDOMOPTION_ENTRY];
	int qty;
};

struct randomoption_group {
	int id;
	struct randomoption_group_optoin options[MAX_ITEM_RANDOMOPTION];
};

struct randomoption_drop_entry {
	short nameid;
	short group;
	short rate;
	unsigned int limit;
};

struct randomoption_drop {
	struct randomoption_drop_entry drops[MAX_ITEM_RANDOMOPTION_DROP];
	int entry;
};

struct randomoption_db {
	int id;
	int bonus;
	int param_index;
	int param_count;
	int var[5];
	struct script_code *script;
};

static struct randomoption_db randomoption_db[MAX_RANDOMOPTION] = { 0, };
static struct randomoption_group randomoption_group_db[MAX_RANDOMOPTION_GROUP] = { 0, };
static struct randomoption_drop randomoption_drop_data[MAX_RANDOMOPTION_DROP] = { 0, };
struct randomoption_drop *randomoption_drop_db = randomoption_drop_data - MOB_ID_MIN;

int randomoption_db_exsits(int id)
{
	if (id < 0 || id >= MAX_RANDOMOPTION)
		return -1;

	return randomoption_db[id].id;
}

int randomoption_group_exsits(int id)
{
	if (id < 0 || id >= MAX_RANDOMOPTION_GROUP)
		return -1;

	return randomoption_group_db[id].id;
}

int randomoption_drop_exsits(int id)
{
	if (id < MOB_ID_MIN || id >= MOB_ID_MAX)
		return -1;

	return randomoption_drop_db[id].entry > 0 ? id : 0;
}

void randomoption_set_var(struct script_code *script, const char *name, int val)
{
	char prefix = *name;
	char postfix = name[strlen(name) - 1];

	if (prefix == '\'' && postfix != '$') {
		int num;
		char *old_var;

		num = script_add_str(name);

		if (val != 0) {
			old_var = linkdb_replace(&script->script_vars, INT2PTR(num), INT2PTR(val));
		}
		else {
			old_var = linkdb_erase(&script->script_vars, INT2PTR(num));
		}

		if (old_var)
			aFree(old_var);
	}
}

void randomoption_run_script(struct map_session_data * sd, struct script_code * script, int val)
{
	nullpo_retv(sd);
	nullpo_retv(script);

	randomoption_set_var(script, "'option_val", val);
	run_script(script, 0, sd->bl.id, 0);
}

int randomoption_drop_sub(int tid, unsigned int tick, int id, void *data)
{
	struct delay_item_drop2 *ditem;

	nullpo_retr(0, ditem = (struct delay_item_drop2 *)data);

	while (ditem != NULL)
	{
		struct delay_item_drop2 *next = ditem->next;

		if (ditem->first_id > 0) {
			struct map_session_data *sd = map_id2sd(ditem->first_id);
			if (sd && sd->bl.prev && sd->state.autoloot && !unit_isdead(&sd->bl) && sd->bl.m == ditem->m) {
				int flag;
				struct party *p = NULL;

				if (sd->status.party_id > 0) {
					p = party_search(sd->status.party_id);
				}
				if ((flag = party_loot_share(p, sd, &ditem->item_data, sd->bl.id)) != 0) {
					clif_additem(sd, 0, 0, flag);
				}
				else {
					aFree(ditem);
					continue;
				}
			}
		}

		map_addflooritem(
			&ditem->item_data, ditem->item_data.amount, ditem->m, ditem->x, ditem->y,
			ditem->first_id, ditem->second_id, ditem->third_id, 0
		);

		aFree(ditem);

		ditem = next;
	}

	return 0;
}

void randomoption_dropitem(struct mob_data * md, unsigned int tick, int first_id, int second_id, int third_id)
{
	int i, j, k;
	int pos;
	struct item_data *item;
	struct delay_item_drop2 *head = NULL;
	struct randomoption_drop *rnd_drop = NULL;

	if (!randomoption_drop_exsits(md->class_))
	{
		return;
	}
	
	rnd_drop = &randomoption_drop_db[md->class_];

	for (i = 0; i < randomoption_drop_db[md->class_].entry; ++i)
	{
		int rnd, cnt;
		struct item drop;
		struct delay_item_drop2 *ditem;
		struct randomoption_drop_entry *entry = &rnd_drop->drops[i];

		rnd = atn_rand() % MAX_ITEM_RANDOMOPTION_RATE;
		if (entry->rate <= rnd)
			continue;

		item = itemdb_exists(entry->nameid);
		if (item == NULL)
			continue;

		memset(&drop, 0, sizeof(drop));

		for (j = 0, pos = 0; j < MAX_ITEM_RANDOMOPTION; ++j)
		{
			cnt = randomoption_group_db[entry->group].options[j].qty;
			if (cnt > 0 && randomoption_group_db[entry->group].options[j].option[cnt - 1].qty > 0)
			{
				rnd = atn_rand() % randomoption_group_db[entry->group].options[j].option[cnt - 1].qty;
				for (k = 0; k < cnt && rnd >= randomoption_group_db[entry->group].options[j].option[k].qty; ++k);
				rnd = atn_rand() % MAX_ITEM_RANDOMOPTION_RATE;
				if (randomoption_group_db[entry->group].options[j].option[k].rate <= rnd)
					continue;

				if (randomoption_group_db[entry->group].options[j].option[k].value_max - randomoption_group_db[entry->group].options[j].option[k].value_min <= 0)
				{
					rnd = 0;
				}
				else
				{
					rnd = randomoption_group_db[entry->group].options[j].option[k].value_min +
						(atn_rand()
							% (randomoption_group_db[entry->group].options[j].option[k].value_max
								- randomoption_group_db[entry->group].options[j].option[k].value_min));
				}

				drop.opt[pos].id = randomoption_group_db[entry->group].options[j].option[k].option;
				drop.opt[pos].val = rnd;
				++pos;
			}
		}

		drop.nameid = item->nameid;
		drop.amount = 1;

		if (randomoption_drop_db[md->class_].drops[i].limit > 0)
		{
			drop.limit = (unsigned int)time(NULL) + randomoption_drop_db[md->class_].drops[i].limit;
			drop.identify = 1;
		}
		else
		{
			drop.limit = 0; 
			drop.identify = !itemdb_isequip3(item->nameid);
			if (battle_config.itemidentify)
				drop.identify = 1;
		}

		ditem = (struct delay_item_drop2 *)aCalloc(1, sizeof(struct delay_item_drop2));
		memcpy(&ditem->item_data, &drop, sizeof(drop));
		ditem->m = md->bl.m;
		ditem->x = md->bl.x;
		ditem->y = md->bl.y;
		ditem->first_id = first_id;
		ditem->second_id = second_id;
		ditem->third_id = third_id;
		ditem->next = head;
		head = ditem;
	}

	if (head)
		add_timer(tick + 560, randomoption_drop_sub, 0, head);
}

void randomoption_calc_pc(struct map_session_data * sd)
{
	int i, idx;
	nullpo_retv(sd);
	for (i = 0; i < EQUIP_INDEX_MAX; i++) {
		if (i == EQUIP_INDEX_ARROW)
			continue;
		idx = sd->equip_index[i];

		if (idx < 0)
			continue;
		if (i == EQUIP_INDEX_RARM && sd->equip_index[EQUIP_INDEX_LARM] == idx)
			continue;
		if (i == EQUIP_INDEX_HEAD3 && sd->equip_index[EQUIP_INDEX_HEAD] == idx)
			continue;
		if (i == EQUIP_INDEX_HEAD2 && (sd->equip_index[EQUIP_INDEX_HEAD3] == idx || sd->equip_index[EQUIP_INDEX_HEAD] == idx))
			continue;
		if (i == EQUIP_INDEX_COSTUME_HEAD3 && sd->equip_index[EQUIP_INDEX_COSTUME_HEAD] == idx)
			continue;
		if (i == EQUIP_INDEX_COSTUME_HEAD2 && (sd->equip_index[EQUIP_INDEX_COSTUME_HEAD3] == idx || sd->equip_index[EQUIP_INDEX_COSTUME_HEAD] == idx))
			continue;

		if (sd->inventory_data[idx]) {
			if (itemdb_isequip(sd->inventory_data[idx]->nameid))
			{
				randomoption_calc_item(sd, &sd->status.inventory[idx]);
			}
		}
	}
}

void randomoption_calc_item(struct map_session_data * sd, struct item * item)
{
	int i;
	nullpo_retv(sd);
	nullpo_retv(item);

	for (i = 0; i < MAX_ITEM_RANDOMOPTION; ++i)
	{
		randomoption_cruuent_opt = item->opt[i].id;
		randomoption_current_val = item->opt[i].val;

		if (randomoption_db_exsits(randomoption_cruuent_opt) == randomoption_cruuent_opt)
		{
			if (randomoption_db[randomoption_cruuent_opt].script != NULL)
			{
				randomoption_run_script(sd, randomoption_db[randomoption_cruuent_opt].script, randomoption_current_val);
			}
			else
			{
				int var[5] = { 0, };
				memcpy(var, randomoption_db[randomoption_cruuent_opt].var, sizeof(var));
				if (randomoption_db[randomoption_cruuent_opt].param_index >= 0 && randomoption_db[randomoption_cruuent_opt].param_index < 5) {
					var[randomoption_db[randomoption_cruuent_opt].param_index] = randomoption_current_val;
				}

				switch (randomoption_db[randomoption_cruuent_opt].param_count) {
				case 1:
					RANDOMOPTION_BONUS1(sd, randomoption_db[randomoption_cruuent_opt].bonus, var[0]);
					break;
				case 2:
					RANDOMOPTION_BONUS2(sd, randomoption_db[randomoption_cruuent_opt].bonus, var[0], var[1]);
					break;
				case 3:
					RANDOMOPTION_BONUS3(sd, randomoption_db[randomoption_cruuent_opt].bonus, var[0], var[1], var[2]);
					break;
				case 4:
					RANDOMOPTION_BONUS4(sd, randomoption_db[randomoption_cruuent_opt].bonus, var[0], var[1], var[2], var[3]);
					break;
				case 5:
					RANDOMOPTION_BONUS5(sd, randomoption_db[randomoption_cruuent_opt].bonus, var[0], var[1], var[2], var[3], var[4]);
					break;
				}
			}
		}

		randomoption_cruuent_opt = 0;
		randomoption_current_val = 0;
	}
}

char* trim(char* str)
{
	size_t start;
	size_t end;

	if (str == NULL)
		return str;

	for (start = 0; str[start] && isspace(str[start]); ++start)
		;
	// get end position
	for (end = strlen(str); start < end && str[end - 1] && isspace(str[end - 1]); --end)
		;
	// trim
	if (start == end)
		*str = '\0';// empty string
	else
	{// move string with nul terminator
		str[end] = '\0';
		//memmove(str, str + start, end - start + 1);
		str += start;
	}
	return str;
}

int get_int(char *str)
{
	int val = 0;
	char *p = trim(str);

	if (str[0] == '?')
	{
		return 0;
	}

	if (isdigit(p[0]))
	{
		val = atoi(p);
	}
	else
	{
		script_get_constant(p, &val);
	}

	return val;
}

int randomoption_read_db(void)
{
	FILE *fp;
	char line[4096];
	int ln = 0, lines = 0;
	int id, j, num = 0, idx = 0;
	char *str[8], *p, *np, *tmp;
	struct script_code *script = NULL;
	int i = 0;
	const char *filename = "db/random_option_db.txt";

	fp = fopen(filename, "r");
	if (fp == NULL) {
		printf("randomoption_read_db: open [%s] failed !\n", filename);
		return 0;
	}
	lines = ln = 0;
	while (fgets(line, sizeof(line), fp)) {
		lines++;
		if (line[0] == '\0' || line[0] == '\r' || line[0] == '\n')
			continue;
		if (line[0] == '/' && line[1] == '/')
			continue;
		memset(str, 0, sizeof(str));
		np = p = line;
		str[0] = p;
		if (p = strchr(p, ',')) { *p++ = 0; np = p; };
		if (str[0] == NULL)
			continue;

		id = get_int(str[0]);
		if (id <= 0 || id >= MAX_RANDOMOPTION)
			continue;

		randomoption_db[id].id = id;

		if ((tmp = strchr(np, '{')) != NULL)
		{
			np = parse_script_line_end(tmp, filename, lines);
			if (!np)
				continue;

			if (randomoption_db[id].script) {
				script_free_code(randomoption_db[id].script);
			}
			script = parse_script(tmp, filename, lines);
			randomoption_db[id].script = (script_is_error(script)) ? NULL : script;

			continue;
		}

		idx = 0;

		for (j = 1; j < 7 && p; j++) {
			str[j] = p;
			p = strchr(p, ',');

			if (j > 1 && strchr(str[j], '?') != NULL)
			{
				idx = j;
			}

			if (p)
			{
				*p++ = '\0';
				np = p;
			}
			else
			{
				break;
			}
		}

		if (idx < 2)
		{
			idx = 1;
		}

		//parse bonus
		num = get_int(str[1]);

		if (num <= 0)
		{
			randomoption_db[id].id = 0;
			continue;
		}

		randomoption_db[id].bonus = num;
		randomoption_db[id].param_count = j - 1;
		randomoption_db[id].param_index = idx - 2;

		for (i = 0; i < randomoption_db[id].param_count; ++i)
		{
			if (i == randomoption_db[id].param_index)
				continue;
			randomoption_db[id].var[i] = get_int(str[i + 2]);
		}

		ln++;
	}
	fclose(fp);
	printf("read %s done (count=%d)\n", filename, ln);


	return 0;
}

int randomoption_read_group(void)
{
	FILE *fp;
	char line[4096];
	int ln = 0, lines = 0;
	int id, j, num = 0, idx = 0, min = 0, max = 0, qty = 0, rate = 0;
	char *str[8], *p, *np;
	int entry = 0;
	const char *filename = "db/random_option_group.txt";

	fp = fopen(filename, "r");
	if (fp == NULL) {
		printf("randomoption_read_db: open [%s] failed !\n", filename);
		return 0;
	}
	ln = 0;
	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == '\0' || line[0] == '\r' || line[0] == '\n')
			continue;
		if (line[0] == '/' && line[1] == '/')
			continue;
		memset(str, 0, sizeof(str));
		for (j = 0, np = p = line; j < 7 && p; j++) {
			str[j] = p;
			p = strchr(p, ',');
			if (p) { *p++ = 0; np = p; }
		}
		if (j < 5 || str[0] == NULL || str[2] == NULL)
			continue;

		id = get_int(str[0]) - 1;
		if (id < 0 || id >= MAX_RANDOMOPTION_GROUP)
			continue;

		idx = atoi(str[1]) - 1;
		num = get_int(str[2]);
		qty = atoi(str[5]);
		rate = j > 6 ? atoi(str[6]) : MAX_ITEM_RANDOMOPTION_RATE;

		if (rate > MAX_ITEM_RANDOMOPTION_RATE)
		{
			rate = MAX_ITEM_RANDOMOPTION_RATE;
		}

		if (idx < 0 || num <= 0 || qty <= 0 || rate <= 0)
			continue;

		if (randomoption_db_exsits(num) == num)
		{
			min = atoi(str[3]);
			max = atoi(str[4]);

			if (min < 0 || max < 0)
				continue;

			entry = randomoption_group_db[id].options[idx].qty;
			if (entry >= MAX_RANDOMOPTION_ENTRY)
				continue;
			if (entry > 0)
				qty += randomoption_group_db[id].options[idx].option[entry - 1].qty;
			if (qty > MAX_ITEM_RANDOMOPTION_RATE)
				continue;

			if (min > max)
			{
				int tmp = min;
				min = max;
				max = tmp;
			}

			randomoption_group_db[id].options[idx].option[entry].option = num;
			randomoption_group_db[id].options[idx].option[entry].value_min = min;
			randomoption_group_db[id].options[idx].option[entry].value_max = max;
			randomoption_group_db[id].options[idx].option[entry].qty = qty;
			randomoption_group_db[id].options[idx].option[entry].rate = rate;
			randomoption_group_db[id].options[idx].qty++;
		}
		else
		{
			continue;
		}

		randomoption_group_db[id].id = id;

		ln++;
	}
	fclose(fp);
	printf("read %s (count=%d)\n", filename, ln);

	return 0;
}

int randomoption_read_drop(void)
{
	FILE *fp;
	char line[4096];
	int ln = 0, lines = 0;
	int id, j, num = 0, rate = 0, limit = 0;
	char *str[8], *p, *np;
	struct item_data *item = NULL;
	int entry = 0;
	const char *filename = "db/random_option_drop.txt";

	fp = fopen(filename, "r");
	if (fp == NULL) {
		printf("randomoption_read_db: open [%s] failed !\n", filename);
		return 0;
	}
	ln = 0;
	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == '\0' || line[0] == '\r' || line[0] == '\n')
			continue;
		if (line[0] == '/' && line[1] == '/')
			continue;
		memset(str, 0, sizeof(str));
		for (j = 0, np = p = line; j < 5 && p; j++) {
			str[j] = p;
			p = strchr(p, ',');
			if (p) { *p++ = 0; np = p; }
		}
		if (str[0] == NULL)
			continue;

		id = atoi(str[0]);
		if (id <= 0 || id != mobdb_checkid(id))
			continue;

		item = itemdb_exists(atoi(str[1]));
		if (item == NULL || !itemdb_isequip(item->nameid))
			continue;

		num = atoi(str[2]) - 1;
		if (num < 0 || randomoption_group_exsits(num) < 0)
			continue;

		rate = atoi(str[3]);

		if (rate <= 0)
			continue;

		limit = atoi(str[4]);

		if (rate > MAX_ITEM_RANDOMOPTION_RATE)
			rate = MAX_ITEM_RANDOMOPTION_RATE;

		if (limit < 0)
			limit = 0;

		entry = randomoption_drop_db[id].entry;

		if (entry >= MAX_ITEM_RANDOMOPTION_DROP)
			continue;

		randomoption_drop_db[id].drops[entry].nameid = item->nameid;
		randomoption_drop_db[id].drops[entry].group = num;
		randomoption_drop_db[id].drops[entry].rate = rate;
		randomoption_drop_db[id].drops[entry].limit = limit;
		randomoption_drop_db[id].entry++;
		ln++;
	}
	fclose(fp);
	printf("read %s (count=%d)\n", filename, ln);

	return 0;
}

void randomoption_read(void)
{
	randomoption_read_db();
	randomoption_read_group();
	randomoption_read_drop();
}

void randomoption_reload(void)
{
	memset(randomoption_group_db, 0, sizeof(randomoption_group_db));
	memset(randomoption_drop_data, 0, sizeof(randomoption_drop_data));
	memset(randomoption_db, 0, sizeof(randomoption_db));

	randomoption_read();
}

void do_final_randomoption(void)
{
	int i;

	for (i = 0; i < MAX_RANDOMOPTION; ++i) {
		if (randomoption_db[i].script) {
			script_free_code(randomoption_db[i].script);
		}
	}
}

void do_init_randomoption(void)
{
	randomoption_reload();
}
