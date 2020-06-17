/*
    IIP Zoomify Request Command Handler Class Member Function for blending multiple channels

    * Development carried out thanks to R&D grant DC08P02OUK006 - Old Maps Online *
    * (www.oldmapsonline.org) from Ministry of Culture of the Czech Republic      *


    Copyright (C) 2008-2015 Ruven Pillay.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include <cmath>

#include "Task.h"
#include "Transforms.h"
#include "Tokenizer.h"
#include <stdio.h>

using namespace std;

typedef struct
{
    const char* single_channel[1] = {"00FF00"};  // green
    const char* two_channel[2] = {"00FF00", "FF0000" };  // green, red
    const char* three_channel[3] = {"0000FF", "00FF00", "FF0000" };  // blue, green, red
}DefaultColors;

typedef struct
{
    unsigned int idx;  // temporarily define here
    const char* lut;  // colormap string or HEX-code
    unsigned int min;  // not used yet
    unsigned int max;  // not used yet
}BlendingSetting;

void ZoomifyBlend::run( Session* session, const std::string& argument ){
    printf("Boom!\n");
    // hard coded variables
    BlendingSetting settings[3];
    DefaultColors def_colors;

    settings[0].idx = 10;
    settings[1].idx = 11;
    settings[2].idx = 12;

    settings[0].lut = def_colors.three_channel[0];
    settings[1].lut = def_colors.three_channel[1];
    settings[2].lut = def_colors.three_channel[2];

    // next steps: try to blend 2 channels, naive addition f.i.
    unsigned int n_ch = 2;

  if( session->loglevel >= 3 ) (*session->logfile) << "ZoomifyBlend handler reached" << endl;
  // if( session->loglevel >= 4 ) (*session->logfile) << "Argument string:\n" << argument << "\n" << endl;

  // Time this command
  if( session->loglevel >= 2 ) command_timer.start();


  // The argument is in the form ZoomifyBlend=TileGroup0/r-x-y.jpg where r is the resolution
  // number and x and y are the tile coordinates starting from the bottom left.
  string prefix, suffix;
  suffix = argument.substr( argument.find_last_of( "/" )+1, argument.length() );

    if( session->loglevel >= 4 ) (*session->logfile) << "DEBUG suffix:\n" << suffix << "\n" << endl;

  // We need to extract the image path, which is not always the same
  if( suffix == "ImageProperties.xml" )
    prefix = argument.substr( 0, argument.find_last_of( "/" ) );
  else
    prefix = argument.substr( 0, argument.find( "TileGroup" )-1 );

    if( session->loglevel >= 4 ) (*session->logfile) << "DEBUG prefix:\n" << prefix << "\n" << endl;

  // for each image (channel) do.... (first create unique "prefix" and check if it is in cache!)

  // As we don't have an independent FIF request, we need to run it now
  FIF fif;
  fif.run( session, prefix ); // read image to session, if cached, read from cache


  // Get the full image size and the total number of resolutions available
  unsigned int width = (*session->image)->getImageWidth();
  unsigned int height = (*session->image)->getImageHeight();


  unsigned int tw = (*session->image)->getTileWidth();
  unsigned int numResolutions = (*session->image)->getNumResolutions();


  // Zoomify does not accept arbitrary numbers of resolutions. The lowest
  // level must be the largest size that can fit within a single tile, so
  // we must discard any smaller than this
  unsigned int n;

  unsigned int discard = 0;

  for( n=0; n<numResolutions; n++ ){
    if( (*session->image)->image_widths[n] < tw && (*session->image)->image_heights[n] < tw ){
      discard++;
    }
  }


  if( discard > 0 ) discard -= 1;

  if( session->loglevel >= 2 ){
    if( discard > 0 ){
      *(session->logfile) << "Zoomify :: Discarding " << discard << " resolutions that are too small for Zoomify" << endl;
    }
  }

  // Zoomify clients have 2 phases, the initialization phase where they request
  // an XML file containing image data and the tile requests themselves.
  // These 2 phases are handled separately
  if( suffix == "ImageProperties.xml" ){

    if( session->loglevel >= 2 ){
      *(session->logfile) << "Zoomify :: ImageProperties.xml request" << endl;
      *(session->logfile) << "Zoomify :: Total resolutions: " << numResolutions << ", image width: " << width
			  << ", image height: " << height << endl;
    }

    int ntiles = (int) ceil( (double)width/tw ) * (int) ceil( (double)height/tw );

    char str[1024];
    snprintf( str, 1024,
	      "Server: iipsrv/%s\r\n"
	      "Content-Type: application/xml\r\n"
	      "Last-Modified: %s\r\n"
	      "%s\r\n"
	      "\r\n"
	      "<IMAGE_PROPERTIES WIDTH=\"%d\" HEIGHT=\"%d\" NUMTILES=\"%d\" NUMIMAGES=\"1\" VERSION=\"1.8\" TILESIZE=\"%d\" />",
	      VERSION, (*session->image)->getTimestamp().c_str(), session->response->getCacheControl().c_str(), width, height, ntiles, tw );

    session->out->printf( (const char*) str );
    session->response->setImageSent();

    return;
  }


  // Get the tile coordinates. Zoomify requests are of the form r-x-y.jpg
  // where r is the resolution number and x and y are the tile coordinates
  Tokenizer izer( suffix, "-" );
  int resolution=0, x=0, y=0;
  if( izer.hasMoreTokens() ) resolution = atoi( izer.nextToken().c_str() );
  if( izer.hasMoreTokens() ) x = atoi( izer.nextToken().c_str() );
  if( izer.hasMoreTokens() ) y = atoi( izer.nextToken().c_str() );

  // Bump up to take account of any levels too small for Zoomify
  resolution += discard;

  if( session->loglevel >= 2 ){
    *(session->logfile) << "Zoomify :: Tile request for resolution:"
			<< resolution << " at x:" << x << ", y:" << y << endl;
  }


  // Get the width and height for the requested resolution
  width = (*session->image)->getImageWidth(numResolutions-resolution-1);
  height = (*session->image)->getImageHeight(numResolutions-resolution-1);


  // Get the width of the tiles and calculate the number
  // of tiles in each direction
  unsigned int rem_x = width % tw;
  unsigned int ntlx = (width / tw) + (rem_x == 0 ? 0 : 1);


  // Calculate the tile index for this resolution from our x, y
  unsigned int tile = y*ntlx + x;


  // Simply pass this on to our JTL send command
  JTL jtl;
  jtl.send( session, resolution, tile );  // TODO


  // Total Zoomify response time
  if( session->loglevel >= 2 ){
    *(session->logfile) << "Zoomify :: Total command time " << command_timer.getTime() << " microseconds" << endl;
  }


}
