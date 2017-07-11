/*
Copyright (c) 2010-2016, Mathieu Labbe - IntRoLab - Universite de Sherbrooke
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Universite de Sherbrooke nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 * The methods in this file were modified from the originals of the MRPT toolkit (see notice below):
 * https://github.com/MRPT/mrpt/blob/master/libs/topography/src/conversions.cpp
 */

/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2016, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */


#include "rtabmap/core/GeodeticCoords.h"

#include <cmath>
#ifndef M_PI
#define M_PI       3.14159265358979323846
#endif

namespace rtabmap {


inline double DEG2RAD(const double x) { return x*M_PI/180.0;}
inline double square(const double & value) {return value*value;}

//*---------------------------------------------------------------
//			geodeticToGeocentric_WGS84
// ---------------------------------------------------------------*/
cv::Point3d GeodeticCoords::toGeocentric_WGS84() const
{
	// --------------------------------------------------------------------
	// See: http://en.wikipedia.org/wiki/Reference_ellipsoid
	//  Constants are for WGS84
	// --------------------------------------------------------------------

	static const double a = 6378137;		// Semi-major axis of the Earth (meters)
	static const double b = 6356752.3142;	// Semi-minor axis:

	static const double ae = acos(b/a);  	// eccentricity:
	static const double cos2_ae_earth =  square(cos(ae)); // The cos^2 of the angular eccentricity of the Earth: // 0.993305619995739L;
	static const double sin2_ae_earth = square(sin(ae));  // The sin^2 of the angular eccentricity of the Earth: // 0.006694380004261L;

	const double lon  = DEG2RAD( double(this->longitude()) );
	const double lat  = DEG2RAD( double(this->latitude()) );

	// The radius of curvature in the prime vertical:
	const double N = a / std::sqrt( 1.0 - sin2_ae_earth*square( sin(lat) ) );

	// Generate 3D point:
	cv::Point3d out;
	out.x = (N+this->altitude())*cos(lat)*cos(lon);
	out.y = (N+this->altitude())*cos(lat)*sin(lon);
	out.z = (cos2_ae_earth*N+this->altitude())*sin(lat);

	return out;
}


/*---------------------------------------------------------------
				geodeticToENU_WGS84
 ---------------------------------------------------------------*/
cv::Point3d GeodeticCoords::toENU_WGS84(const GeodeticCoords &origin) const
{
	// --------------------------------------------------------------------
	//  Explanation: We compute the earth-centric coordinates first,
	//    then make a system transformation to local XYZ coordinates
	//    using a system of three orthogonal vectors as local reference.
	//
	// See: http://en.wikipedia.org/wiki/Reference_ellipsoid
	// (JLBC 21/DEC/2006)  (Fixed: JLBC 9/JUL/2008)
	// - Oct/2013, Emilio Sanjurjo: Fixed UP vector pointing exactly normal to ellipsoid surface.
	// --------------------------------------------------------------------
	// Generate 3D point:
	cv::Point3d	P_geocentric = this->toGeocentric_WGS84();

	// Generate reference 3D point:
	cv::Point3d P_geocentric_ref = origin.toGeocentric_WGS84();

	const double clat = cos(DEG2RAD(origin.latitude())), slat = sin(DEG2RAD(origin.latitude()));
	const double clon = cos(DEG2RAD(origin.longitude())), slon = sin(DEG2RAD(origin.longitude()));

	// Compute the resulting relative coordinates:
	// For using smaller numbers:
	P_geocentric -= P_geocentric_ref;

	// Optimized calculation: Local transformed coordinates of P_geo(x,y,z)
	//   after rotation given by the transposed rotation matrix from ENU -> ECEF.
	cv::Point3d out;
	out.x = -slon*P_geocentric.x + clon*P_geocentric.y;
	out.y = -clon*slat*P_geocentric.x -slon*slat*P_geocentric.y + clat*P_geocentric.z;
	out.z = clon*clat*P_geocentric.x + slon*clat*P_geocentric.y +slat*P_geocentric.z;

	return out;
}

GeodeticCoords::GeodeticCoords() :
		latitude_(0.0),
		longitude_(0.0),
		altitude_(0.0)
{

}
GeodeticCoords::GeodeticCoords(double latitude, double longitude, double altitude) :
		latitude_(latitude),
		longitude_(longitude),
		altitude_(altitude)
{

}

}
