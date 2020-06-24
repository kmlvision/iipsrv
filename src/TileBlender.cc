/*
    Tile Blending Functions
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
#include <jansson.h> // used for parsing json strings
#include "TileBlender.h"
#include "Task.h"


using namespace std;

bool TileBlender::loadBlendingSettingsFromJSon(const char* string_to_parse, std::vector<BlendingSetting> &blending_settings)
{
  json_t* j_root;
  json_error_t j_error;

  j_root = json_loads(string_to_parse, 0, &j_error);
  if ( !j_root )
  {
    fprintf( stderr, "error: on line %d: %s\n", j_error.line, j_error.text );
    return false;
  }

  const char* key;
  json_t* value;
  const char* sub_key;
  json_t* sub_value;

  // iterate through json objects and create blending settings
  void *iter = json_object_iter( j_root );
  while(iter)
  {
    key = json_object_iter_key(iter);
    value = json_object_iter_value(iter);

    BlendingSetting setting;
    setting.idx = atoi(key);

    // get expected json objects
    json_t* obj = json_object_get(value, "lut");
    if(!obj)
      return false;
    const char* tmp_lut = json_string_value(obj)+1;
    setting.lut = std::string(tmp_lut);
    if(setting.lut.length() != 6)
      return false;

    obj = json_object_get(value, "min");
    if(!obj)
      return false;
    setting.min = static_cast<unsigned int>(json_integer_value(obj));
    if(setting.min < 0)
      return false;

    obj = json_object_get(value, "max");
    if(!obj)
      return false;
    setting.max = static_cast<unsigned int>(json_integer_value(obj));
    if(setting.max <= 0 || setting.max <= setting.min)
      return false;

    //printf("Created setting with index: %d: lut=%s, min=%d, max=%d\n", setting.idx, setting.lut.c_str(), setting.min, setting.max );
    blending_settings.push_back(setting);

    iter = json_object_iter_next(j_root, iter);
  }

  if(j_root) {
    json_decref(j_root);
  }

  return true;
}


void TileBlender::getRawTilesAndPreprocess(Session* session, int resolution, int tile, const std::vector<BlendingSetting> &blending_settings)
{
  // timer for individual functions
  Timer function_timer;

  // get raw tiles and preprocess
  for (int i = 0; i < session->images.size(); ++i) {
    IIPImage *image = session->images[i];

    if (image->getColourSpace() != GREYSCALE || image->channels != 1 || (image->bpc != 16 && image->bpc != 8)) {
      throw string("TileBlender :: only 16/8bit grayscale images supported");
    }
    // for each image:
    // 1. get tiles (from cache)
    TileManager tilemanager(session->tileCache, image, session->watermark, session->jpeg, session->logfile,
                            session->loglevel);

    // First calculate histogram if we have asked for either binarization,
    //  histogram equalization or contrast stretching
    if (session->view->requireHistogram() && image->histogram.size() == 0) {

      if (session->loglevel >= 4) function_timer.start();

      // Retrieve an uncompressed version of our smallest tile
      // which should be sufficient for calculating the histogram
      RawTile thumbnail = tilemanager.getTile(0, 0, 0, session->view->yangle, session->view->getLayers(), UNCOMPRESSED);

      // Calculate histogram
      image->histogram =
              session->processor->histogram(thumbnail, image->max, image->min);

      if (session->loglevel >= 4) {
        *(session->logfile) << "TileBlender :: Calculated histogram in "
                            << function_timer.getTime() << " microseconds" << endl;
      }

      // Insert the histogram into our image cache
      const string key = image->getImagePath();
      imageCacheMapType::iterator i = session->imageCache->find(key);
      if (i != session->imageCache->end()) (i->second).histogram = image->histogram;
    }

    CompressionType ct;
    // Request uncompressed tile if raw pixel data is required for processing
    if (
            image->getNumBitsPerPixel() >= 8 ||   // image->getNumBitsPerPixel() > 8
            image->getColourSpace() == CIELAB ||
            image->getNumChannels() == 2 ||
            image->getNumChannels() > 3 ||
            (session->view->colourspace == GREYSCALE && image->getNumChannels() == 3 && image->getNumBitsPerPixel() == 8) ||
            session->view->floatProcessing() ||
            session->view->equalization ||
            session->view->getRotation() != 0.0 ||
            session->view->flip != 0)
      ct = UNCOMPRESSED;
    else ct = JPEG;


    // Embed ICC profile
    if (session->view->embedICC() && (image->getMetadata("icc").size() > 0)) {
      if (session->loglevel >= 3) {
        *(session->logfile) << "TileBlender :: Embedding ICC profile with size "
                            << image->getMetadata("icc").size() << " bytes" << endl;
      }
      session->jpeg->setICCProfile(image->getMetadata("icc"));
    }


    RawTile rawtile = tilemanager.getTile(resolution, tile, session->view->xangle,
                                          session->view->yangle, session->view->getLayers(), ct);

    if (rawtile.compressionType != UNCOMPRESSED) {
      throw string(
              "TileBlender :: rawtile.compressionType -> retrieved image data already compressed, uncompressed data buffer required");
    }

    // 2. preprocess each tile (min/max contrast stretching)  TODO: check all preprocessing steps if they make sense for the blending case....
    // Only use our float pipeline if necessary
    if (rawtile.bpc >= 8 || session->view->floatProcessing()) {
      if (session->loglevel >= 5) {
        function_timer.start();
      }

      // Make a copy of our max and min as we may change these
      vector<float> min;
      vector<float> max;

      // Change our image max and min if we have asked for a contrast stretch
//      if (session->view->contrast == -1) {
//
//        // Find first non-zero bin in histogram
//        unsigned int n0 = 0;
//        while (image->histogram[n0] == 0) ++n0;
//
//        // Find highest bin
//        unsigned int n1 = image->histogram.size() - 1;
//        while (image->histogram[n1] == 0) --n1;
//
//        // Histogram has been calculated using 8 bits, so scale up to native bit depth
//        if (rawtile.bpc > 8 && rawtile.sampleType == FIXEDPOINT) {
//          n0 = n0 << (rawtile.bpc - 8);
//          n1 = n1 << (rawtile.bpc - 8);
//        }
//
//        min.assign(rawtile.bpc, (float) n0);
//        max.assign(rawtile.bpc, (float) n1);
//
//        // Reset our contrast
//        session->view->contrast = 1.0;
//
//        if (session->loglevel >= 5) {
//          *(session->logfile) << "TileBlender :: Applying contrast stretch for image range of "
//                              << n0 << " - " << n1 << " in "
//                              << function_timer.getTime() << " microseconds" << endl;
//        }
//
//      }

      // Apply normalization and float conversion
      // assign given min/max values for histogram stretching
      min.push_back(blending_settings[i].min);
      max.push_back(blending_settings[i].max);
      if (session->loglevel >= 4) {
        *(session->logfile) << "TileBlender :: Normalizing between [" << min[0] << ", " << max[0] <<
                            "] and converting to float";
        function_timer.start();
      }
      session->processor->normalize(rawtile, max, min);
      if (session->loglevel >= 4) {
        *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
      }


      // Apply hill shading if requested
      if (session->view->shaded) {
        if (session->loglevel >= 4) {
          *(session->logfile) << "TileBlender :: Applying hill-shading";
          function_timer.start();
        }
        session->processor->shade(rawtile, session->view->shade[0], session->view->shade[1]);
        if (session->loglevel >= 4) {
          *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
        }
      }


      // Apply color twist if requested
      if (session->view->ctw.size()) {
        if (session->loglevel >= 4) {
          *(session->logfile) << "TileBlender :: Applying color twist";
          function_timer.start();
        }
        session->processor->twist(rawtile, session->view->ctw);
        if (session->loglevel >= 4) {
          *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
        }
      }


      // Apply any gamma correction
      if (session->view->gamma != 1.0) {
        float gamma = session->view->gamma;
        if (session->loglevel >= 4) {
          *(session->logfile) << "TileBlender :: Applying gamma of " << gamma;
          function_timer.start();
        }
        session->processor->gamma(rawtile, gamma);
        if (session->loglevel >= 4) {
          *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
        }
      }


      // Apply inversion if requested
      if (session->view->inverted) {
        if (session->loglevel >= 4) {
          *(session->logfile) << "TileBlender :: Applying inversion";
          function_timer.start();
        }
        session->processor->inv(rawtile);
        if (session->loglevel >= 4) {
          *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
        }
      }


      // Apply color mapping if requested
      if (session->view->cmapped) {
        if (session->loglevel >= 4) {
          *(session->logfile) << "TileBlender :: Applying color map";
          function_timer.start();
        }
        session->processor->cmap(rawtile, session->view->cmap);
        if (session->loglevel >= 4) {
          *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
        }
      }


      // Apply any contrast adjustments and/or clip to 8bit from 16 or 32 bit
      float contrast = session->view->contrast;
      if (session->loglevel >= 4) {
        *(session->logfile) << "TileBlender :: Applying contrast of " << contrast
                            << " and converting to 8 bit";
        function_timer.start();
      }
      session->processor->contrast(rawtile, contrast);
      if (session->loglevel >= 4) {
        *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
      }
    }
    // end tile float processing
    // start tile processing

    // Reduce to 1 or 3 bands if we have an alpha channel or a multi-band image
    if (rawtile.channels == 2 || rawtile.channels > 3) {
      unsigned int bands = (rawtile.channels == 2) ? 1 : 3;
      if (session->loglevel >= 4) {
        *(session->logfile) << "TileBlender :: Flattening channels to " << bands;
        function_timer.start();
      }
      session->processor->flatten(rawtile, bands);
      if (session->loglevel >= 4) {
        *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
      }
    }


    // Convert to greyscale if requested
    if ((*session->image)->getColourSpace() == sRGB && session->view->colourspace == GREYSCALE) {
      if (session->loglevel >= 4) {
        *(session->logfile) << "TileBlender :: Converting to greyscale";
        function_timer.start();
      }
      session->processor->greyscale(rawtile);
      if (session->loglevel >= 4) {
        *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
      }
    }


    // Convert to binary (bi-level) if requested
    if (session->view->colourspace == BINARY) {
      if (session->loglevel >= 4) {
        *(session->logfile) << "TileBlender :: Converting to binary with threshold ";
        function_timer.start();
      }
      unsigned int threshold = session->processor->threshold(image->histogram);
      session->processor->binary(rawtile, threshold);
      if (session->loglevel >= 4) {
        *(session->logfile) << threshold << " in " << function_timer.getTime() << " microseconds" << endl;
      }
    }


    // Apply histogram equalization
    if (session->view->equalization) {
      if (session->loglevel >= 4) function_timer.start();
      // Perform histogram equalization
      session->processor->equalize(rawtile, image->histogram);
      if (session->loglevel >= 4) {
        *(session->logfile) << "TileBlender :: Applying histogram equalization in "
                            << function_timer.getTime() << " microseconds" << endl;
      }
    }

    // Apply flip
    if (session->view->flip != 0) {
      Timer flip_timer;
      if (session->loglevel >= 5) {
        flip_timer.start();
      }

      session->processor->flip(rawtile, session->view->flip);

      if (session->loglevel >= 5) {
        *(session->logfile) << "TileBlender :: Flipping tile ";
        if (session->view->flip == 1) *(session->logfile) << "horizontally";
        else *(session->logfile) << "vertically";
        *(session->logfile) << " in " << flip_timer.getTime() << " microseconds" << endl;
      }
    }

    // Apply rotation - can apply this safely after gamma and contrast adjustment
    if (session->view->getRotation() != 0.0) {
      float rotation = session->view->getRotation();
      if (session->loglevel >= 4) {
        *(session->logfile) << "TileBlender :: Rotating tile by " << rotation << " degrees";
        function_timer.start();
      }
      session->processor->rotate(rawtile, rotation);
      if (session->loglevel >= 4) {
        *(session->logfile) << " in " << function_timer.getTime() << " microseconds" << endl;
      }
    }

    this->raw_tiles.push_back(rawtile);  // rawtile added to raw_tiles vector, tiles that are all ready to be blended
  } // end foreach image
}


void TileBlender::blend(Session* session, int resolution, int tile, const std::vector<BlendingSetting> &blending_settings)
{
  if (session->loglevel >= 2) {
    *(session->logfile) << "TileBlender :: reached" << endl;
  }

  // sanity check
  if(session->images.size() != blending_settings.size())
  {
    std::string error_msg("TileBlender: number of blending parameters differs from the number of available images!");
    session->response->setError( "2 1", error_msg );
    throw error_msg;
  }

  // timer for individual functions
  Timer function_timer;

  // If we have requested a rotation, remap the tile index to rotated coordinates
  if ((int) ((session->view)->getRotation()) % 360 == 90) {

  } else if ((int) ((session->view)->getRotation()) % 360 == 270) {

  } else if ((int) ((session->view)->getRotation()) % 360 == 180) {
    int num_res = (*session->image)->getNumResolutions();
    unsigned int im_width = (*session->image)->image_widths[num_res - resolution - 1];
    unsigned int im_height = (*session->image)->image_heights[num_res - resolution - 1];
    unsigned int tw = (*session->image)->getTileWidth();
    //    unsigned int th = (*session->image)->getTileHeight();
    int ntiles = (int) ceil((double) im_width / tw) * (int) ceil((double) im_height / tw);
    tile = ntiles - tile - 1;
  }

  // Sanity check
  if ((resolution < 0) || (tile < 0)) {
    ostringstream error;
    error << "TileBlender :: Invalid resolution/tile number: " << resolution << "," << tile;
    throw error.str();
  }

  // get raw tiles and preprocess (fill raw_tiles vector)
  this->getRawTilesAndPreprocess(session, resolution, tile, blending_settings);

  // 3. blend tiles by using colors/colormaps -> one output RGB tile
  const int out_channels = 3; // RGB channels
  const RawTile &tmp = raw_tiles[0];

  RawTile blended_tile(0, tmp.resolution, tmp.hSequence, tmp.vSequence, tmp.width, tmp.height, 3, 8);
  blended_tile.dataLength = blended_tile.width * blended_tile.height * out_channels;
  uint8_t *dst = new uint8_t[blended_tile.dataLength]();
  blended_tile.data = dst;  // this is cleaned up by raw tile

  const unsigned int dst_stride = blended_tile.width * out_channels;
  const unsigned int src_stride = blended_tile.width;

  // now blend all tiles together:
  for (int tidx = 0; tidx < raw_tiles.size(); ++tidx) {
    if (session->loglevel >= 4) {
      *(session->logfile) << "TileBlender :: BLENDING tile nr " << tidx << endl;
    }

    const float img_min = (*session->images[tidx]).min[0];
    const float img_max = (*session->images[tidx]).max[0];
    const unsigned int bit_depth = (*session->images[tidx]).bpc;

    if (session->loglevel >= 5) {
      *(session->logfile) << "TileBlender :: original image BitDepth = " << bit_depth << endl;
      *(session->logfile) << "TileBlender :: original image Minimum  = " << img_min << endl;
      *(session->logfile) << "TileBlender :: original image Maximum  = " << img_max << endl;
    }

    const RawTile &cur_tile = raw_tiles[tidx];
    const uint8_t *src = static_cast<const uint8_t *>(cur_tile.data);

    // try to parse color code
    BlendColor b_color;
    try {
      if (session->loglevel >= 5) {
        *(session->logfile) << "TileBlender :: try to parse color code: -> " << blending_settings[tidx].lut
                            << endl;
      }
      int value = std::stoi(blending_settings[tidx].lut, nullptr, 16);
      if (session->loglevel >= 5) {
        *(session->logfile) << "TileBlender :: color code successfully converted to int -> " << value << endl;
      }
      b_color = BlendColor::from_int(value);
    }
    catch (std::exception &) {
      session->response->setError("2 1", blending_settings[tidx].lut);
      throw std::invalid_argument(
              "TileBlender ERROR: invalid color code for TileBlender!");// + blending_settings[tidx].lut );
    }

    for (int y = 0; y < blended_tile.height; ++y) {
      uint8_t *dst_row_p = dst + y * dst_stride;

      for (int x = 0; x < blended_tile.width; ++x) {
        // get the gray value
        auto gv = src[y * src_stride + x];

        // convert to color
        auto r = b_color.r * (gv / (std::pow(2, 8) - 1));
        auto g = b_color.g * (gv / (std::pow(2, 8) - 1));
        auto b = b_color.b * (gv / (std::pow(2, 8) - 1));

        // fill output array and clip value between 0...255
        dst_row_p[x * out_channels] = static_cast<uint8_t>(std::min(255, std::max(0, static_cast<int>(r + dst_row_p[x *
                                                                                                                    out_channels]))));
        dst_row_p[x * out_channels + 1] = static_cast<uint8_t>(std::min(255, std::max(0, static_cast<int>(g + dst_row_p[
                x * out_channels + 1]))));
        dst_row_p[x * out_channels + 2] = static_cast<uint8_t>(std::min(255, std::max(0, static_cast<int>(b + dst_row_p[
                x * out_channels + 2]))));

      }
    }
  }

  unsigned int len = blended_tile.dataLength;
  // Compress to JPEG
  if (blended_tile.compressionType == UNCOMPRESSED) {
    if (session->loglevel >= 4) {
      *(session->logfile) << "TileBlender :: Compressing UNCOMPRESSED blended_tile to JPEG";
      function_timer.start();
    }
    len = session->jpeg->Compress(blended_tile);
    if (session->loglevel >= 4) {
      *(session->logfile) << " in " << function_timer.getTime() << " microseconds to "
                          << blended_tile.dataLength << " bytes" << endl;
    }
  }

#ifndef DEBUG
  char str[1024];

  snprintf(str, 1024,
           "Server: iipsrv/%s\r\n"
           "X-Powered-By: IIPImage\r\n"
           "Content-Type: image/jpeg\r\n"
           "Content-Length: %d\r\n"
           "Last-Modified: %s\r\n"
           "%s\r\n"
           "\r\n",
           VERSION, len, (*session->image)->getTimestamp().c_str(), session->response->getCacheControl().c_str());

  session->out->printf(str);
#endif

  // 4. send final response
  if (session->out->putStr(static_cast<const char *>(blended_tile.data), len) != len) {
    if (session->loglevel >= 1) {
      *(session->logfile) << "TileBlender :: Error writing jpeg tile" << endl;
    }
  }

  if (session->out->flush() == -1) {
    if (session->loglevel >= 1) {
      *(session->logfile) << "TileBlender :: Error flushing jpeg tile" << endl;
    }
  }
}