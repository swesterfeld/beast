/* BSE - Bedevilled Sound Engine                        -*-mode: c++;-*-
 * Copyright (C) 2003 Tim Janik
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __BSE_CXX_CLOSURE_H__
#define __BSE_CXX_CLOSURE_H__

#include <bse/bsecxxvalue.hh>
#include <bse/bsecxxarg.hh>

namespace Bse {

class CxxClosure {
  GClosure             *glib_closure;
  CxxClosure&           operator=       (const CxxClosure &c);
  explicit              CxxClosure      (const CxxClosure &c);
protected:
  String                sig_tokens;
  virtual void          operator()      (Value            *return_value,
                                         const Value      *param_values,
                                         gpointer          invocation_hint,
                                         gpointer          marshal_data) = 0;
public:
  explicit              CxxClosure      ();
  virtual               ~CxxClosure     ();
  GClosure*             gclosure        ();
  const String          signature       () { return sig_tokens; }
};

/* include generated CxxClosure* Closure (class T*, ... (T::*f) (...)); constructors */
#include <bse/bsegenclosures.h>

} // Bse

#endif /* __BSE_CXX_CLOSURE_H__ */
