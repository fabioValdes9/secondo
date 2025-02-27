//This file is part of SECONDO.

//Copyright (C) 2014, University in Hagen, Department of Computer Science,
//Database Systems for New Applications.

//SECONDO is free software; you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation; either version 2 of the License, or
//(at your option) any later version.

//SECONDO is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with SECONDO; if not, write to the Free Software
//Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

package unittests.mmdb.suites;

import unittests.mmdb.data.MemoryRelationTests;
import unittests.mmdb.data.attributes.MemoryAttributeTests;
import unittests.mmdb.data.attributes.date.AttributeDateTests;
import unittests.mmdb.data.attributes.date.AttributeInstantTests;
import unittests.mmdb.data.attributes.instant.InstantAttributeTests;
import unittests.mmdb.data.attributes.moving.MovingAttributeTests;
import unittests.mmdb.data.attributes.range.AttributePeriodsTests;
import unittests.mmdb.data.attributes.range.RangeAttributeTests;
import unittests.mmdb.data.attributes.spatial.AttributeLineTests;
import unittests.mmdb.data.attributes.spatial.AttributePointTests;
import unittests.mmdb.data.attributes.spatial.AttributePointsTests;
import unittests.mmdb.data.attributes.spatial.AttributeRectTests;
import unittests.mmdb.data.attributes.spatial.AttributeRegionTests;
import unittests.mmdb.data.attributes.standard.AttributeBoolTests;
import unittests.mmdb.data.attributes.standard.AttributeIntTests;
import unittests.mmdb.data.attributes.standard.AttributeRealTests;
import unittests.mmdb.data.attributes.standard.AttributeStringTests;
import unittests.mmdb.data.attributes.standard.AttributeTextTests;
import unittests.mmdb.data.attributes.unit.UnitAttributeTests;
import unittests.mmdb.data.attributes.util.MovableUnitObjectsTests.MovableFaceTests;
import unittests.mmdb.data.attributes.util.MovableUnitObjectsTests.MovableRealTests;
import unittests.mmdb.data.attributes.util.MovableUnitObjectsTests.MovableRegionTests;
import unittests.mmdb.data.attributes.util.SpatialObjectsTests.FaceTests;
import unittests.mmdb.data.attributes.util.SpatialObjectsTests.SegmentTests;
import unittests.mmdb.data.attributes.util.TemporalObjectsTests.PeriodTests;
import unittests.mmdb.data.indices.MemoryIndexTests;

import org.junit.runner.RunWith;
import org.junit.runners.Suite;
import org.junit.runners.Suite.SuiteClasses;

/**
 * Suite for collecting all tests within the "data" module.
 *
 * @author Alexander Castor
 */
@RunWith(Suite.class)
@SuiteClasses({ MemoryRelationTests.class, MemoryAttributeTests.class, AttributeDateTests.class,
		AttributeInstantTests.class, InstantAttributeTests.class, MovingAttributeTests.class,
		AttributePeriodsTests.class, RangeAttributeTests.class, AttributeLineTests.class,
		AttributePointTests.class, AttributePointsTests.class, AttributeRectTests.class,
		AttributeRegionTests.class, AttributeBoolTests.class, AttributeIntTests.class,
		AttributeRealTests.class, AttributeStringTests.class, AttributeTextTests.class,
		UnitAttributeTests.class, MovableRegionTests.class, MovableFaceTests.class,
		MovableRealTests.class, SegmentTests.class, FaceTests.class, PeriodTests.class,
		MemoryIndexTests.class })
public class TestSuiteData {

}
