//This file is part of SECONDO.

//Copyright (C) 2006, University in Hagen, Department of Computer Science, 
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

// Written 2012 by Jan Kristof Nidzwetzki 

package progresswatcher;

import org.jfree.data.time.TimeSeries;
import org.jfree.data.time.TimeSeriesDataItem;

/**
 * Draw the Estimated Time Graph
 *
 */
public class EstimatedTimeGraph extends AbstractGraph {
	
	protected void calculateDataSeries() {
        seriesOut = new TimeSeries("Estimated Query Time");
		seriesIn = new TimeSeries("Optimal Time");
	}

	@Override
	protected boolean getLineVisibleInLegend() {
		return false;
	}
	
	@Override
	protected String getGraphTitle() {
		return "Estimated Query Time";
	}

	@Override
	protected String getGraphXAxisLegend() {
		return "Time";
	}

	@Override
	protected String getGraphYAxisLegend() {
		return "Estimated Time";
	}
	
	@Override
	protected boolean enableAutoRange() {
		return true;
	}

	@Override
	protected double getOptimalMinValue(final TimeSeriesDataItem beginItem, 
			final TimeSeriesDataItem endItem) {
		
		return endItem.getPeriod().getMiddleMillisecond() 
			- beginItem.getPeriod().getMiddleMillisecond();
	}

	@Override
	protected double getOptimalMaxValue(final TimeSeriesDataItem beginItem,
			final TimeSeriesDataItem endItem) {
		
		return endItem.getPeriod().getMiddleMillisecond() 
			- beginItem.getPeriod().getMiddleMillisecond();
	}

	@Override
	protected WindowType getWindowTypeForGraph() {
		return WindowType.TIME;
	}
}
