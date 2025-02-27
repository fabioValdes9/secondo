/*

1.1.1 Class Implementation

----
This file is part of SECONDO.

Copyright (C) 2017,
Faculty of Mathematics and Computer Science,
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
#include "catch.hh" // https://github.com/catchorg/Catch2

#include "Algebras/DBService2/DBServiceManager.hpp"

using namespace DBService;
using namespace std;

TEST_CASE("Testing DBService::DBServiceManager")
{
  // SECTION("Obtain DBServiceManager Instance") {
  //   DBServiceManager* dbService = DBServiceManager::getInstance();
  //   REQUIRE(dbService != nullptr);
  // }

  // SECTION("replicaExists") {
  //   SECTION("replicaExists shouldn't find a non-existing replica") {
  //     DBServiceManager* dbService = DBServiceManager::getInstance();
  //     dbService->replicaExists("nonexistingdb", "nonexistingrelation");
  //   }
  // }
}