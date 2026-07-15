/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE for the full license.
 */

#include "options.h"

#include <stdlib.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace
{
   using namespace zutty;

   // std::map keeps references to stored values stable. Options keeps several
   // const char* pointers into this storage for the lifetime of the process.
   std::map <std::string, std::string> commandLine;

   const OptionDesc*
   findOption (const char* prefix)
   {
      // Keep the long-standing -v shorthand after adding -vulkanInfo.
      if (strcmp (prefix, "v") == 0)
         prefix = "verbose";

      const OptionDesc* found = nullptr;
      const size_t n = strlen (prefix);

      for (const auto& option: optionsTable)
      {
         if (strncmp (option.option, prefix, n) != 0)
            continue;

         if (strlen (option.option) == n)
            return &option;
         if (found != nullptr)
            throw std::runtime_error (
               std::string ("ambiguous option: ") + prefix);
         found = &option;
      }
      return found;
   }

   bool
   isAdvancedOption (const char* name)
   {
      for (const auto& resource: resourceTable)
         if (strcmp (resource.resource, name) == 0)
            return true;
      return false;
   }

   const char*
   get (const char* name, const char* fallback = nullptr,
        OptionSource* src = nullptr)
   {
      auto withSource = [=] (const OptionSource source, const char* value)
      {
         if (src != nullptr)
            *src = source;
         return value;
      };

      const auto parsed = commandLine.find (name);
      if (parsed != commandLine.end ())
         return withSource (OptionSource::CmdLine, parsed->second.c_str ());

      for (const auto& option: optionsTable)
         if (strcmp (option.option, name) == 0 && option.hardDefault != nullptr)
            return withSource (OptionSource::HardDefault, option.hardDefault);

      for (const auto& resource: resourceTable)
         if (strcmp (resource.resource, name) == 0 && resource.hardDefault != nullptr)
            return withSource (OptionSource::HardDefault, resource.hardDefault);

      return withSource (OptionSource::NONE, fallback);
   }

   void
   getBorder (uint16_t& outBorder)
   {
      const char* option = get ("border");
      std::stringstream input (option != nullptr ? option : "");
      int border;
      input >> border;
      if (input.fail () || border < 0 || border > 3000)
         throw std::runtime_error ("-border: expected unsigned, max. 3000");
      outBorder = border;
   }

   void
   getSaveLines (uint16_t& outSaveLines)
   {
      const char* option = get ("saveLines");
      std::stringstream input (option != nullptr ? option : "");
      int lines;
      input >> lines;
      if (input.fail () || lines < 0 || lines > 50000)
         throw std::runtime_error ("-saveLines: expected unsigned, max. 50000");
      outSaveLines = lines;
   }

   void
   getFontsize (uint8_t& outFontsize)
   {
      const char* option = get ("fontsize");
      std::stringstream input (option != nullptr ? option : "");
      int size;
      input >> size;
      if (input.fail () || size < 1 || size > 255)
         throw std::runtime_error ("-fontsize: expected integer within 1..255");
      outFontsize = size;
   }

   void
   getGeometry (uint16_t& outCols, uint16_t& outRows)
   {
      const char* option = get ("geometry");
      std::stringstream input (option != nullptr ? option : "");
      int cols;
      int rows;
      char separator;
      input >> cols >> separator >> rows;
      if (input.fail () || separator != 'x' || cols < 1 || rows < 1)
         throw std::runtime_error ("-geometry: expected format <COLS>x<ROWS>");
      outCols = cols;
      outRows = rows;
   }

   uint8_t
   convHexDigit (const char* name, const char ch)
   {
      if (ch >= '0' && ch <= '9')
         return ch - '0';
      if (ch >= 'a' && ch <= 'f')
         return ch - 'a' + 10;
      if (ch >= 'A' && ch <= 'F')
         return ch - 'A' + 10;

      throw std::runtime_error (std::string ("-") + name +
                                ": illegal hex digit; expected hex RGB color");
   }

   void
   convColor (const char* name, const char* option, zutty::Color& outColor)
   {
      const char* value = option [0] == '#' ? option + 1 : option;
      switch (strlen (value))
      {
      case 3:
         outColor.red = 17 * convHexDigit (name, value [0]);
         outColor.green = 17 * convHexDigit (name, value [1]);
         outColor.blue = 17 * convHexDigit (name, value [2]);
         break;
      case 6:
         outColor.red = (convHexDigit (name, value [0]) << 4) +
                        convHexDigit (name, value [1]);
         outColor.green = (convHexDigit (name, value [2]) << 4) +
                          convHexDigit (name, value [3]);
         outColor.blue = (convHexDigit (name, value [4]) << 4) +
                         convHexDigit (name, value [5]);
         break;
      default:
         throw std::runtime_error (std::string ("-") + name +
                                   ": expected hex RGB color");
      }
   }

} // namespace

zutty::Options opts;

namespace zutty
{
   void
   Options::initialize (int* argc, char** argv)
   {
      int output = 1;

      for (int input = 1; input < *argc; ++input)
      {
         const char* argument = argv [input];
         if ((argument [0] != '-' && argument [0] != '+') || argument [1] == '\0')
         {
            argv [output++] = argv [input];
            continue;
         }

         const bool enabled = argument [0] == '-';
         const char* name = argument + 1;

         if (strcmp (name, "e") == 0)
         {
            while (input < *argc)
               argv [output++] = argv [input++];
            break;
         }

         const OptionDesc* option = findOption (name);
         if (option == nullptr)
         {
            if (!isAdvancedOption (name))
               throw std::runtime_error (std::string ("unknown option: ") + argument);

            if (input + 1 >= *argc)
               throw std::runtime_error (std::string (argument) + ": missing value");
            commandLine [name] = argv [++input];
            continue;
         }

         switch (option->parseType)
         {
         case OptionKind::NoArg:
            commandLine [option->option] = enabled ? option->implValue : "false";
            break;
         case OptionKind::SepArg:
            if (!enabled)
               throw std::runtime_error (std::string (argument) + ": '+' is invalid here");
            if (input + 1 >= *argc)
               throw std::runtime_error (std::string (argument) + ": missing value");
            commandLine [option->option] = argv [++input];
            break;
         case OptionKind::SkipLine:
            break;
         }
      }

      *argc = output;
      argv [output] = nullptr;
   }

   bool
   Options::getBool (const char* name, bool defaultValue)
   {
      const char* option = get (name);
      if (option == nullptr)
         return defaultValue;
      if (strcmp (option, "true") == 0)
         return true;
      if (strcmp (option, "false") == 0)
         return false;
      throw std::runtime_error (std::string ("-") + name +
                                ": expected true or false");
   }

   void
   Options::getColor (const char* name, zutty::Color& outColor)
   {
      const char* option = get (name);
      if (option == nullptr)
         throw std::runtime_error (std::string ("-") + name + ": missing value");
      convColor (name, option, outColor);
   }

   int
   Options::getInteger (const char* name, int min, int max)
   {
      const char* option = get (name);
      if (option == nullptr)
         return min;

      std::stringstream input (option);
      int result;
      input >> result;
      if (input.fail ())
         throw std::runtime_error (std::string ("-") + name + ": expected integer");
      return std::min (std::max (min, result), max);
   }

   void
   Options::handlePrintOpts ()
   {
      if (getBool ("help"))
      {
         printUsage ();
         exit (0);
      }
      if (getBool ("listres"))
      {
         printResources ();
         exit (0);
      }
   }

   void
   Options::parse ()
   {
      handlePrintOpts ();
      try
      {
         getBorder (border);
         getSaveLines (saveLines);
         dwfontname = get ("dwfont");
         fontname = get ("font");
         fontpath = get ("fontpath");
         getFontsize (fontsize);
         getGeometry (nCols, nRows);
         vulkanInfo = getBool ("vulkanInfo");
         shell = get ("shell", getenv ("SHELL"));
         if (shell == nullptr)
            shell = "bash";
         title = get ("title", nullptr, &titleSource);
         getColor ("fg", fg);
         getColor ("bg", bg);
         rv = getBool ("rv");
         if (rv)
            std::swap (fg, bg);
         if (get ("cr") != nullptr)
            getColor ("cr", cr);
         else
            cr = fg;
         altScrollMode = getBool ("altScroll");
         altSendsEscape = getBool ("altSendsEscape");
         autoCopyMode = getBool ("autoCopy");
         boldColors = getBool ("boldColors");
         login = getBool ("login");
         showWraps = getBool ("showWraps");
         quiet = getBool ("quiet");
         verbose = getBool ("verbose");
         modifyOtherKeys = getInteger ("modifyOtherKeys", 0, 2);
      }
      catch (const std::exception& error)
      {
         std::cout << "Error: " << error.what () << "!\n"
                   << "Try -help for usage options." << std::endl;
         exit (-1);
      }
   }

   void
   Options::printVersion () const
   {
      std::cout << "Zutty " ZUTTY_VERSION "\n"
                << "Copyright (C) 2020 Tom Szilagyi\n\n"
                << "This program comes with ABSOLUTELY NO WARRANTY.\n"
                << "Zutty is free software, and you are welcome to redistribute it\n"
                << "under the terms and conditions of the GNU GPL v3 (or later).\n"
                << std::endl;
   }

   void
   Options::printUsage () const
   {
      printVersion ();
      std::cout << "Usage:\n"
                << "  zutty [-option ...] [shell]\n\n"
                << "Options:\n";
      size_t maxWidth = 0;
      for (const auto& option: optionsTable)
         maxWidth = std::max (maxWidth, strlen (option.option));
      for (const auto& option: optionsTable)
      {
         std::cout << "  -" << std::left << std::setw (maxWidth + 3)
                   << option.option << option.helpDescr;
         if (option.hardDefault != nullptr && option.parseType != OptionKind::NoArg)
            std::cout << " (default: " << option.hardDefault << ")";
         std::cout << "\n";
      }
      std::cout << std::endl;
   }

   void
   Options::printResources () const
   {
      printVersion ();
      std::cout << "Advanced options:\n";
      size_t maxWidth = 0;
      for (const auto& resource: resourceTable)
         maxWidth = std::max (maxWidth, strlen (resource.resource));
      for (const auto& resource: resourceTable)
      {
         std::cout << "  -" << std::left << std::setw (maxWidth + 3)
                   << resource.resource << resource.helpDescr;
         if (resource.hardDefault != nullptr)
            std::cout << " (default: " << resource.hardDefault << ")";
         std::cout << "\n";
      }
      std::cout << std::endl;
   }

} // namespace zutty
