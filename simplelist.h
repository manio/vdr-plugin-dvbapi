/*
 *  vdr-plugin-dvbapi - softcam dvbapi plugin for VDR
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef ___SIMPLELIST_H
#define ___SIMPLELIST_H

class cSimpleListBase;

class cSimpleItem
{
  friend class cSimpleListBase;

private:
  cSimpleItem *next;

public:
  virtual ~cSimpleItem()
  {
  }
  cSimpleItem *Next(void) const
  {
    return next;
  }
};

class cSimpleListBase
{
protected:
  cSimpleItem *first, *last;
  int count;

public:
  cSimpleListBase(void);
  ~cSimpleListBase();
  void Add(cSimpleItem *Item, cSimpleItem *After = 0);
  void Ins(cSimpleItem *Item);
  void Del(cSimpleItem *Item, bool Del = true);
  void Clear(void);
  int Count(void) const
  {
    return count;
  }
};

template<class T> class cSimpleList : public cSimpleListBase
{
public:
  T *First(void) const
  {
    return (T *) first;
  }
  T *Last(void) const
  {
    return (T *) last;
  }
  T *Next(const T *item) const
  {
    return (T *) item->cSimpleItem::Next();
  }
};

#endif // ___SIMPLELIST_H
