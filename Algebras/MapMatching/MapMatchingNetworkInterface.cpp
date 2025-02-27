
/*
----
This file is part of SECONDO.

Copyright (C) 2018, 
University in Hagen, 
Faculty of Mathematics and  Computer Science,
Database Systems for New Applications.

SECONDO is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

SECONDO is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with SECONDO; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
----

*/


#include "MapMatchingNetworkInterface.h"



namespace mapmatch {


std::ostream& operator<<(std::ostream&o, 
                         const IMMNetworkSection::EDirection& dir) {


  switch(dir){
    case IMMNetworkSection::DIR_NONE: o << "DIR_NONE"; break;
    case IMMNetworkSection::DIR_UP: o << "DIR_UP"; break;
    case IMMNetworkSection::DIR_DOWN: o << "DIR_DOWN"; break;
    default: assert(false);
  }
  return o;
}

} // end of namespace 

