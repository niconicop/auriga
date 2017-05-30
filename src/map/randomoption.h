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

#ifndef	_RANDOMOPTION_H_
#define	_RANDOMOPTION_H_

extern int randomoption_cruuent_opt;
extern int randomoption_current_val;

void randomoption_dropitem(struct mob_data *md, unsigned int tick, int first_id, int second_id, int third_id);
void randomoption_calc_pc(struct map_session_data *sd);
void randomoption_calc_item(struct map_session_data *sd, struct item *item);

void randomoption_reload(void);

void do_final_randomoption(void);
void do_init_randomoption(void);

#endif	// _RANDOMOPTION_H_
