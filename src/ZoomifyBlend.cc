/*
    IIP Zoomify Request Command Handler Class Member Function for blending multchannel images.

    Copyright (C) 2020 KML Vision GmbH.
*/

#include <string>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdio.h>
#include <sstream>
#include <vector>
#include <limits.h>

#include "Task.h"
#include "TileManager.h"
#include "Transforms.h"
#include "Tokenizer.h"
#include "TileBlender.h"

using namespace std;

void ZoomifyBlend::run(Session *session, const std::string &argument) {
    if (session->loglevel >= 3) (*session->logfile) << "ZoomifyBlend :: handler reached\n" << endl;
    if (session->loglevel >= 4) (*session->logfile) << "ZoomifyBlend :: Argument string:\n" << argument << "\n" << endl;

    if (argument.find("&") == std::string::npos) {
        session->response->setError("2 0", argument);
        throw string("ZoomifyBlend: check argument string to contain all data");
    }

    // Time this command
    if (session->loglevel >= 2) command_timer.start();

    // set local session pointer
    this->session = session;

    // get json string by splitting the argument into zoomify params and the json string
    std::string zoomify_params(argument.substr(0, argument.find("&")));
    std::string json_string(argument.substr(argument.find("&") + 1, argument.length()));
    if (session->loglevel >= 4) {
        (*session->logfile) << "ZoomifyBlend :: Zoomify params:\n" << zoomify_params << "\n" << endl;
        (*session->logfile) << "ZoomifyBlend :: JSON string:\n" << json_string << "\n" << endl;
    }
    if (session->loglevel >= 3) (*session->logfile) << "ZoomifyBlend :: parsing json string\n" << endl;

    // create tile blender
    TileBlender tile_blender;

    // parse blending_settings
    std::vector<BlendingSetting> blending_settings;
    if (!tile_blender.loadBlendingSettingsFromJson(json_string.c_str(), blending_settings)) {
        session->response->setError("2 1", argument);
        throw string("ZoomifyBlend: check json syntax");
    }
    int blending_size = blending_settings.size();
    if (blending_size == 0) {
        session->response->setError("2 3", argument);
        throw string("ZoomifyBlend: blend settings empty");
    }
    if (session->loglevel >= 5) (*session->logfile) << "ZoomifyBlend :: successfully parsed json string\n" << endl;

    if (session->loglevel >= 4)
        for (int i = 0; i < blending_size; ++i) {
            *(session->logfile) << "ZoomifyBlend :: Blend settings: Idx="
                                << i << " lut=" << blending_settings[i].lut << ", min=" << blending_settings[i].min
                                << " max=" << blending_settings[i].max << endl;
        }

    // The argument is in the form "/some/path/to/file[.ext]/TileGroup0/r-x-y.jpg&BlendJSON" where r is the resolution
    // number and x and y are the tile coordinates starting from the bottom left. BlendJSON defines a serialized JSON
    // string with the blending settings, we just split the string. The path to the file may optionally have an extension.
    string cmd_filename_prefix, file_ext, suffix;
    suffix = zoomify_params.substr(zoomify_params.find_last_of("/") + 1, zoomify_params.length());

    if (session->loglevel >= 4) (*session->logfile) << "ZoomifyBlend :: suffix: " << suffix << "\n" << endl;

    // We need to extract the image path, which is not always the same
    if (suffix == "ImageProperties.xml")
        cmd_filename_prefix = zoomify_params.substr(0, zoomify_params.find_last_of("/"));
    else {
        // get the file path specification
        cmd_filename_prefix = zoomify_params.substr(0, zoomify_params.find("TileGroup") - 1);

        // desired pattern: "/filename_X.ext", with X ... 0 to N-1 (N ... total number of channels)
        size_t ext_delimiter_idx = cmd_filename_prefix.find_last_of(".");
        if (ext_delimiter_idx != std::string::npos) {
            file_ext = cmd_filename_prefix.substr(ext_delimiter_idx + 1, cmd_filename_prefix.length());
            cmd_filename_prefix = cmd_filename_prefix.substr(0, ext_delimiter_idx);
        }
    }

    if (session->loglevel >= 4) {
        (*session->logfile) << "ZoomifyBlend :: cmd_filename_prefix: " << cmd_filename_prefix << endl;
        (*session->logfile) << "ZoomifyBlend :: file extension: " << file_ext << "\n" << endl;
    }
    // As we don't have an independent FIF request, we need to create it now
    FIF fif;

    // create a list of filenames and load images
    for (unsigned int image_idx = 0; image_idx < blending_size; ++image_idx) {
        char filename[128];
        char idx_as_str[8];
        sprintf(idx_as_str, "_%d", blending_settings[image_idx].idx);

        strcpy(filename, cmd_filename_prefix.c_str());
        strcat(filename, idx_as_str);
        if (!file_ext.empty()) {
            strcat(filename, ("." + file_ext).c_str());
        }
        if (session->loglevel >= 5) {
            (*session->logfile) << "\nZoomifyBlend :: using filename: " << filename << endl;
        }

        // read image to session, if cached, read from cache
        // add images to session->images vector
        fif.run(session, filename);
    }

    // check if session->image is set
    checkImage();

    if (session->loglevel >= 5) {
        (*session->logfile) << "\nZoomifyBlend :: final session-images.size() = " << session->images.size() << "\n"
                            << endl;
    }

    // ###################################################################################################################
    // Get the Zoomify basics:
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

    for (n = 0; n < numResolutions; n++) {
        if ((*session->image)->image_widths[n] < tw && (*session->image)->image_heights[n] < tw) {
            discard++;
        }
    }

    if (discard > 0) discard -= 1;

    if (session->loglevel >= 2) {
        if (discard > 0) {
            *(session->logfile) << "ZoomifyBlend :: Discarding " << discard
                                << " resolutions that are too small for Zoomify" << endl;
        }
    }

    // Zoomify clients have 2 phases, the initialization phase where they request
    // an XML file containing image data and the tile requests themselves.
    // These 2 phases are handled separately
    if (suffix == "ImageProperties.xml") {

        if (session->loglevel >= 2) {
            *(session->logfile) << "ZoomifyBlend :: ImageProperties.xml request" << endl;
            *(session->logfile) << "ZoomifyBlend :: Total resolutions: " << numResolutions << ", image width: " << width
                                << ", image height: " << height << endl;
        }

        int ntiles = (int) ceil((double) width / tw) * (int) ceil((double) height / tw);

        char str[1024];
        snprintf(str, 1024,
                 "Server: iipsrv/%s\r\n"
                 "Content-Type: application/xml\r\n"
                 "Last-Modified: %s\r\n"
                 "%s\r\n"
                 "\r\n"
                 "<IMAGE_PROPERTIES WIDTH=\"%d\" HEIGHT=\"%d\" NUMTILES=\"%d\" NUMIMAGES=\"1\" VERSION=\"1.8\" TILESIZE=\"%d\" />",
                 VERSION, (*session->image)->getTimestamp().c_str(), session->response->getCacheControl().c_str(),
                 width, height, ntiles, tw);

        session->out->printf((const char *) str);
        session->response->setImageSent();

        return;
    }


    // Get the tile coordinates. Zoomify requests are of the form r-x-y.jpg
    // where r is the resolution number and x and y are the tile coordinates
    Tokenizer izer(suffix, "-");
    int resolution = 0, x = 0, y = 0;
    if (izer.hasMoreTokens()) resolution = atoi(izer.nextToken().c_str());
    if (izer.hasMoreTokens()) x = atoi(izer.nextToken().c_str());
    if (izer.hasMoreTokens()) y = atoi(izer.nextToken().c_str());

    // Bump up to take account of any levels too small for Zoomify
    resolution += discard;

    if (session->loglevel >= 2) {
        *(session->logfile) << "ZoomifyBlend :: Tile request for resolution:"
                            << resolution << " at x:" << x << ", y:" << y << endl;
    }

    // Get the width and height for the requested resolution
    width = (*session->image)->getImageWidth(numResolutions - resolution - 1);
    height = (*session->image)->getImageHeight(numResolutions - resolution - 1);

    // Get the width of the tiles and calculate the number
    // of tiles in each direction
    unsigned int rem_x = width % tw;
    unsigned int ntlx = (width / tw) + (rem_x == 0 ? 0 : 1);

    // Calculate the tile index for this resolution from our x, y
    unsigned int tile = y * ntlx + x;

    // Simply pass this on to our blender and send response
    if (session->loglevel >= 4) {
        *(session->logfile) << "ZoomifyBlend :: call TileBlender" << endl;
    }
    tile_blender.blendTiles(session, resolution, tile, blending_settings);

    // Inform our response object that we have sent something to the client
    session->response->setImageSent();

    // Total Zoomify response time
    if (session->loglevel >= 2) {
        *(session->logfile) << "ZoomifyBlend :: Total command time " << command_timer.getTime() << " microseconds"
                            << endl;
    }
}
