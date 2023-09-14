/*
   Copyright (c) 2025, MariaDB Corporation.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1335  USA */


#ifdef MYSQL_SERVER
#include "mariadb.h"
#include "sql_class.h"


bool sp_cursor_array::get_cursor_by_ref_for_reopen(THD *thd,
                                                   Field *ref,
                                                   uint max_cursors)
{
  uint pos;
  if (!Sp_rcontext_handler::dereference(ref, &pos, elements()))
  {
    /*
      There is already an initialized sp_cursor behind the SYS_REFCURSOR.
      It could be opened earlier.
      Two consequent OPEN (without a CLOSE in between) are allowed
      for SYS_REFCURSORs (unlike for static CURSORs).
      Close the first cursor automatically if it's opened, e.g.:
        OPEN c FOR SELECT 1;
        OPEN c FOR SELECT 2;
      Let's also reuse the same sp_cursor instance
      to guarantee cursor aliasing works as expected:
        OPEN c0 FOR SELECT 1;
        SET c1= c0;           -- Creating an alias
        OPEN c0 FOR SELECT 2; -- Reopening affects both c0 and c1
        FETCH c1 INTO a;      -- Fetches "2", from the second "OPEN c0"
    */
    at(pos).reset_for_reopen(thd);
  }
  else
  {
    /*
      The SYS_REFCURSOR variable is not linked to any sp_cursor instances yet.
      In case this is the very first "OPEN" command for a SYS_REFCURSOR
      called during this SQL session.
      Now search for an unused sp_cursor instance inside sp_cursor_array.
      In case all are already used, raise ER_TOO_MANY_OPEN_CURSORS.
    */
    error_t err= find_unused(max_cursors, &pos);
    switch (err) {
    case sp_cursor_array::ERR_CURSOR_ARRAY_OK:
      break;
    case sp_cursor_array::ERR_CURSOR_ARRAY_EOM:
      return true; // Error is already in DA
    case sp_cursor_array::ERR_CURSOR_ARRAY_TOO_MANY:
      my_error(ER_TOO_MANY_OPEN_CURSORS, MYF(0), max_cursors);
      return true;
    }
    /*
      An unused sp_cursor instance has been found at position "pos".
      Strore the found sp_cursor position to the reference variable
      and reset the sp_cursor.
    */
    ref->set_notnull();
    ref->store(pos);
    at(pos).reset(thd);
  }
  return false;
}

#endif
