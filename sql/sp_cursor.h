/*
   Copyright (c) 2009, 2025, MariaDB Corporation.

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

#ifndef SP_CURSOR_INCLUDED
#define SP_CURSOR_INCLUDED

class Server_side_cursor;

class sp_cursor_statistics
{
protected:
  ulonglong m_fetch_count; // Number of FETCH commands since last OPEN
  ulonglong m_row_count;   // Number of successful FETCH since last OPEN
  bool m_found;            // If last FETCH fetched a row
public:
  sp_cursor_statistics()
   :m_fetch_count(0),
    m_row_count(0),
    m_found(false)
  { }
  bool found() const
  { return m_found; }

  ulonglong row_count() const
  { return m_row_count; }

  ulonglong fetch_count() const
  { return m_fetch_count; }
  void reset() { *this= sp_cursor_statistics(); }
};


class sp_instr_cpush;

/* A mediator between stored procedures and server side cursors */
class sp_lex_keeper;
class sp_cursor: public sp_cursor_statistics
{
private:
  /// An interceptor of cursor result set used to implement
  /// FETCH <cname> INTO <varlist>.
  class Select_fetch_into_spvars: public select_result_interceptor
  {
    List<sp_fetch_target> *m_fetch_target_list;
    uint field_count;
    bool m_view_structure_only;
    bool send_data_to_variable_list(List<sp_fetch_target> &vars,
                                    List<Item> &items);
  public:
    Select_fetch_into_spvars(THD *thd_arg, bool view_structure_only)
     :select_result_interceptor(thd_arg),
      m_view_structure_only(view_structure_only)
    {}
    void reset(THD *thd_arg)
    {
      select_result_interceptor::reinit(thd_arg);
      m_fetch_target_list= NULL;
      field_count= 0;
    }
    uint get_field_count() { return field_count; }
    void set_spvar_list(List<sp_fetch_target> *vars)
    {
      m_fetch_target_list= vars;
    }

    bool send_eof() override { return FALSE; }
    int send_data(List<Item> &items) override;
    int prepare(List<Item> &list, SELECT_LEX_UNIT *u) override;
    bool view_structure_only() const override { return m_view_structure_only; }
};

public:
  sp_cursor()
   :result(NULL, false),
    server_side_cursor(NULL)
  { }
  sp_cursor(THD *thd_arg, bool view_structure_only)
   :result(thd_arg, view_structure_only),
    server_side_cursor(NULL)
  {}

  virtual ~sp_cursor()
  { destroy(); }

  virtual sp_lex_keeper *get_lex_keeper() { return nullptr; }

  int open(THD *thd);

  int close(THD *thd);

  my_bool is_open()
  { return MY_TEST(server_side_cursor); }

  int fetch(THD *, List<sp_fetch_target> *vars, bool error_on_no_data);

  bool export_structure(THD *thd, Row_definition_list *list);

  void reset(THD *thd_arg)
  {
    sp_cursor_statistics::reset();
    result.reinit(thd_arg);
    server_side_cursor= NULL;
  }

  /*
    Reset a cursor before reopening (two OPEN without CLOSE in between).
    This method does not raise ER_SP_CURSOR_ALREADY_OPEN.
    It's used to handle:
      c SYS_REFCURSOR;
      OPEN c FOR SELECT 1;
      OPEN c FOR SELECT 2; -- This is allowed without closing the previous OPEN
  */
  void reset_for_reopen(THD *thd_arg)
  {
    if (is_open())
      close(thd_arg);
    reset(thd_arg);
  }

  virtual sp_instr_cpush *get_push_instr() { return nullptr; }
private:
  Select_fetch_into_spvars result;
  Server_side_cursor *server_side_cursor;
  void destroy();
};


class sp_cursor_array: public Dynamic_array<sp_cursor>
{
public:
  enum error_t
  {
    ERR_CURSOR_ARRAY_OK= 0,
    ERR_CURSOR_ARRAY_EOM= 1,
    ERR_CURSOR_ARRAY_TOO_MANY= 2
  };

protected:
  error_t find_unused(uint max_size, uint *pos)
  {
    for (uint i= 0 ; i < (uint) size(); i++)
    {
      if (!at(i).is_open())
      {
        *pos= i;
        return ERR_CURSOR_ARRAY_OK;
      }
    }
    if ((uint) size() >= max_size)
      return ERR_CURSOR_ARRAY_TOO_MANY;
    if (append(sp_cursor()))
      return ERR_CURSOR_ARRAY_EOM;
    *pos= (uint) size() - 1;
    return ERR_CURSOR_ARRAY_OK;
  }

public:
  sp_cursor_array()
   :Dynamic_array(PSI_INSTRUMENT_MEM, 0)
  {}
  ~sp_cursor_array()
  {
    free();
  }
  sp_cursor *get_cursor_by_ref(Field *ref)
  {
     uint pos;
     if (Sp_rcontext_handler::dereference(ref, &pos, elements()))
       return nullptr;
     return &at(pos);
  }
  bool get_cursor_by_ref_for_reopen(THD *thd, Field *ref, uint max_cursors);
  void close()
  {
    for (uint i= 0; i < (uint) size(); i++)
    {
      if (at(i).is_open())
        at(i).close(NULL);
    }
  }
  void free()
  {
    close();
    free_memory();
  }
};

#endif // SP_CURSOR_INCLUDED
