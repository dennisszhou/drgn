/* Return scope DIEs containing PC address.
   Copyright (C) 2005, 2007, 2015 Red Hat, Inc.
   This file is part of elfutils.

   This file is free software; you can redistribute it and/or modify
   it under the terms of either

     * the GNU Lesser General Public License as published by the Free
       Software Foundation; either version 3 of the License, or (at
       your option) any later version

   or

     * the GNU General Public License as published by the Free
       Software Foundation; either version 2 of the License, or (at
       your option) any later version

   or both in parallel, as here.

   elfutils is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received copies of the GNU General Public License and
   the GNU Lesser General Public License along with this program.  If
   not, see <http://www.gnu.org/licenses/>.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include <stdlib.h>
#include "libdwP.h"
#include <dwarf.h>


struct args
{
  Dwarf_Addr pc;
  Dwarf_Die *scopes;
  unsigned int nscopes;
};

/* Preorder visitor: prune the traversal if this DIE does not contain PC.  */
static int
pc_match (unsigned int depth __attribute__ ((unused)),
	  struct Dwarf_Die_Chain *die, void *arg)
{
  struct args *a = arg;

  if (a->scopes != NULL)
    die->prune = true;
  else
    {
      /* dwarf_haspc returns an error if there are no appropriate attributes.
	 But we use it indiscriminantly instead of presuming which tags can
	 have PC attributes.  So when it fails for that reason, treat it just
	 as a nonmatching return.  */
      int result = INTUSE(dwarf_haspc) (&die->die, a->pc);
      if (result < 0)
	{
	  int error = INTUSE(dwarf_errno) ();
	  if (error != DWARF_E_NOERROR
	      && error != DWARF_E_NO_DEBUG_RANGES
	      && error != DWARF_E_NO_DEBUG_RNGLISTS)
	    {
	      __libdw_seterrno (error);
	      return -1;
	    }
	  result = 0;
	}
      if (result == 0)
    	die->prune = true;
    }

  return 0;
}

/* Postorder visitor: first (innermost) call wins.  */
static int
pc_record (unsigned int depth, struct Dwarf_Die_Chain *die, void *arg)
{
  struct args *a = arg;
  struct Dwarf_Die_Chain *tmp;
  Dwarf_Die *scopes;
  int cnt = 0;
  int tag;

  if (die->prune || a->scopes)
    return 0;

  if (a->scopes == NULL)
    {
      /* We have hit the innermost DIE that contains the target PC.  */
      a->nscopes = depth;
      a->scopes = malloc (a->nscopes * sizeof a->scopes[0]);
      if (a->scopes == NULL)
	{
	  __libdw_seterrno (DWARF_E_NOMEM);
	  return -1;
	}

      tmp = die;
      while (tmp)
        {
          tag = INTUSE (dwarf_tag) (&tmp->die);
	  if (tag == DW_TAG_inlined_subroutine
	      || tag == DW_TAG_subroutine_type
	      || tag == DW_TAG_subprogram)
	  {
	      a->scopes[cnt] = tmp->die;
              cnt++;
	  }
          tmp = tmp->parent;
        }

      a->nscopes = cnt;
      if (!a->nscopes)
            return a->nscopes;

      scopes = realloc (a->scopes, a->nscopes * sizeof a->scopes[0]);
      if (scopes == NULL)
        {
          free(a->scopes);
	  __libdw_seterrno (DWARF_E_NOMEM);
	  return -1;
        }
    }

    return a->nscopes;
}


int
dwarf_getfuncs_pc (Dwarf_Die *cudie, Dwarf_Addr pc, Dwarf_Die **scopes)
{
  if (cudie == NULL)
    return -1;

  struct Dwarf_Die_Chain cu = { .parent = NULL, .die = *cudie };
  struct args a = { .pc = pc };

  int result = __libdw_visit_scopes (0, &cu, NULL, &pc_match, &pc_record, &a);

  if (result > 0)
    *scopes = a.scopes;

  return result;
}
