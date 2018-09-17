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
package mol.datatypes.unit;

import mol.datatypes.base.BaseBool;
import mol.datatypes.interval.Period;

/**
 * This class represents 'UnitBool' objects and is used for 'MovingBool' objects
 * with a constant boolean value over a certain period of time.
 * 
 * @author Markus Fuessel
 */
public class UnitBool extends UnitObjectConst<BaseBool> {

   /**
    * Constructor for an undefined 'UnitBool' object<br>
    * Required for subclasses
    */
   public UnitBool() {
      super(new BaseBool());
   }

   /**
    * Base constructor for a 'UnitBool' object
    * 
    * @param period
    *           - valid time period for this unit
    * @param booleanValue
    *           - the constant boolean value for this unit
    * 
    */
   public UnitBool(final Period period, final boolean booleanValue) {
      this(period, new BaseBool(booleanValue));
   }

   /**
    * Base constructor for a 'UnitBool' object
    * 
    * @param period
    *           - valid time period for this unit
    * @param baseBoolValue
    *           - the constant BaseBool value for this unit
    * 
    */
   public UnitBool(final Period period, final BaseBool baseBoolValue) {
      super(period, baseBoolValue);
   }

   /*
    * (non-Javadoc)
    * 
    * @see mol.datatypes.unit.UnitObject#atPeriod(mol.datatypes.interval.Period)
    */
   @Override
   public UnitBool atPeriod(Period period) {
      Period newPeriod = this.getPeriod().intersection(period);

      if (!newPeriod.isDefined()) {
         return new UnitBool();
      }

      return new UnitBool(newPeriod, getValue());
   }

   /*
    * (non-Javadoc)
    * 
    * @see mol.datatypes.unit.UnitObjectConst#getUndefinedObject()
    */
   @Override
   protected BaseBool getUndefinedObject() {
      return new BaseBool();
   }

}