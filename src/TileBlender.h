/*
    Tile Blending Functions
*/


#ifndef _TILEBLENDER_H
#define _TILEBLENDER_H

#include <vector>
#include "Task.h"

typedef struct
{
  const char* single_channel[1] = {"00FF00"};  // green
  const char* two_channel[2] = {"00FF00", "FF0000" };  // green, red
  const char* three_channel[3] = {"0000FF", "00FF00", "FF0000" };  // blue, green, red
  const char* four_channel[4] = {"0000FF", "00FF00", "FFFF00", "FF0000" };  // blue, green, yellow and red
  const char* five_channel[5] = {"0000FF", "00FFFF", "00FF00", "FFFF00", "FF0000" };  // blue, cyan, green, yellow and red
}DefaultColors;

struct BlendColor {
  uint8_t r, g, b;

  static BlendColor from_int(int color)
  {
    BlendColor ret;
    ret.r = (color & 0xFF0000) >> 16;
    ret.g = (color & 0xFF00) >> 8;
    ret.b = color & 0xFF;
    return ret;
  }
};

typedef struct
{
  unsigned int idx;  // temporarily define here
  std::string lut;  // colormap string or HEX-code
  unsigned int min;  // not used yet
  unsigned int max;  // not used yet
}BlendingSetting;


/// Image Processing Transforms
struct TileBlender {
private:
  std::vector<RawTile> raw_tiles;

public:

  /// Function to parse a json string and to create a BlendingSetting vector
  /** @param string_to_parse : json string to be parsed
      @param blending_settings : BlendingSetting vector to be filled
      @return bool for indicating successful parsing of expected setting
  */
  bool loadBlendingSettingsFromJSon(const char* string_to_parse, std::vector<BlendingSetting> &blending_settings);

  void getRawTilesAndPreprocess(Session* session, int resolution, int tile, const std::vector<BlendingSetting> &blending_settings);

  /// Function to blend image tiles from session->images vector by using the blending settings vector
  /** @param session : current session variable expected to have the images vector set up
      @param resolution : image resolution or pyramid level
      @param tile : tile index
      @param blending_settings : BlendingSetting vector to be filled
  */
  void blend(Session* session, int resolution, int tile, const std::vector<BlendingSetting> &blending_settings);

};

#endif
