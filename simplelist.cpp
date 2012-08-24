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

#include "simplelist.h"

cSimpleListBase::cSimpleListBase(void)
{
  first = last = 0;
  count = 0;
}

cSimpleListBase::~cSimpleListBase()
{
  Clear();
}

void cSimpleListBase::Add(cSimpleItem *Item, cSimpleItem *After)
{
  if (After)
  {
    Item->next = After->next;
    After->next = Item;
  }
  else
  {
    Item->next = 0;
    if (last)
      last->next = Item;
    else
      first = Item;
  }
  if (!Item->next)
    last = Item;
  count++;
}

void cSimpleListBase::Ins(cSimpleItem *Item)
{
  Item->next = first;
  first = Item;
  if (!Item->next)
    last = Item;
  count++;
}

void cSimpleListBase::Del(cSimpleItem *Item, bool Del)
{
  if (first == Item)
  {
    first = Item->next;
    if (!first)
      last = 0;
    count--;
  }
  else
  {
    cSimpleItem *item = first;
    while (item)
    {
      if (item->next == Item)
      {
        item->next = Item->next;
        if (!item->next)
          last = item;
        count--;
        break;
      }
      item = item->next;
    }
  }
  if (Del)
    delete Item;
}

void cSimpleListBase::Clear(void)
{
  while (first)
    Del(first);
  first = last = 0;
  count = 0;
}
