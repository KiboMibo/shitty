/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "base.h"
#include "base64.h"
#include "fontpack.h"
#include "log.h"
#include "options.h"
#include "pty.h"
#include "renderer.h"
#include "utf8.h"
#include "vterm.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <langinfo.h>
#include <limits.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using zutty::Fontpack;
using zutty::MouseTrackingEnc;
using zutty::MouseTrackingMode;
using zutty::MouseTrackingState;
using zutty::Renderer;
using zutty::Vterm;
using zutty::VtKey;
using zutty::VtModifier;

static std::unique_ptr <Fontpack> fontpk;
static std::unique_ptr <Renderer> renderer;
static std::unique_ptr <Vterm> vt;
static SDL_Window* window = nullptr;

namespace
{
   class PtyEventSource
   {
   public:
      PtyEventSource (int ptyFd_, Uint32 eventType_)
         : ptyFd (ptyFd_)
         , eventType (eventType_)
      {
         if (pipe (wakePipe) < 0)
            throw std::runtime_error (
               std::string ("pipe failed: ") + std::strerror (errno));
         for (const int fd: wakePipe)
         {
            const int flags = fcntl (fd, F_GETFD);
            if (flags >= 0)
               fcntl (fd, F_SETFD, flags | FD_CLOEXEC);
         }
         worker = std::thread (&PtyEventSource::run, this);
      }

      ~PtyEventSource ()
      {
         {
            std::lock_guard <std::mutex> lock (mutex);
            stopping = true;
            pending = false;
         }
         condition.notify_all ();
         const uint8_t byte = 1;
         while (write (wakePipe [1], &byte, sizeof (byte)) < 0 &&
                errno == EINTR)
            ;
         if (worker.joinable ())
            worker.join ();
         close (wakePipe [0]);
         close (wakePipe [1]);
      }

      void acknowledge ()
      {
         {
            std::lock_guard <std::mutex> lock (mutex);
            pending = false;
         }
         condition.notify_one ();
      }

   private:
      int ptyFd;
      Uint32 eventType;
      int wakePipe [2] {-1, -1};
      std::thread worker;
      std::mutex mutex;
      std::condition_variable condition;
      bool stopping = false;
      bool pending = false;

      void run ()
      {
         struct pollfd pollSet [] = {
            {ptyFd, POLLIN | POLLHUP, 0},
            {wakePipe [0], POLLIN, 0},
         };

         while (true)
         {
            const int result = poll (pollSet, 2, -1);
            if (result < 0)
            {
               if (errno == EINTR)
                  continue;
               return;
            }
            if (pollSet [1].revents & POLLIN)
               return;
            if (!(pollSet [0].revents & (POLLIN | POLLHUP | POLLERR)))
               continue;

            {
               std::lock_guard <std::mutex> lock (mutex);
               if (stopping)
                  return;
               pending = true;
            }

            SDL_Event event {};
            event.type = eventType;
            if (!SDL_PushEvent (&event))
            {
               std::lock_guard <std::mutex> lock (mutex);
               pending = false;
               continue;
            }

            std::unique_lock <std::mutex> lock (mutex);
            condition.wait (lock, [this] { return stopping || !pending; });
            if (stopping)
               return;
         }
      }
   };

   struct MouseContext
   {
      bool selectionOngoing = false;
   } mouseContext;

   enum class MouseEventType
   {
      Press,
      Release,
      Motion
   };

   void resolveShell (char* progPath)
   {
      char resolvedPath [PATH_MAX];
      if (progPath [0] == '/')
         return;
      if (progPath [0] == '.' &&
          realpath (progPath, resolvedPath) != nullptr)
      {
         std::strncpy (progPath, resolvedPath, PATH_MAX - 1);
         progPath [PATH_MAX - 1] = '\0';
         return;
      }

      const char* pathValue = getenv ("PATH");
      char* path = pathValue != nullptr ? strdup (pathValue) : nullptr;
      if (path != nullptr)
      {
         char testPath [PATH_MAX + 1];
         char* part = std::strtok (path, ":");
         while (part != nullptr)
         {
            std::snprintf (testPath, sizeof (testPath), "%s/%s",
                           part, progPath);
            if (realpath (testPath, resolvedPath) != nullptr)
            {
               std::strncpy (progPath, resolvedPath, PATH_MAX - 1);
               progPath [PATH_MAX - 1] = '\0';
               free (path);
               return;
            }
            part = std::strtok (nullptr, ":");
         }
         free (path);
      }

      const char* shell = getenv ("SHELL");
      struct stat statbuf {};
      if (shell != nullptr && stat (shell, &statbuf) == 0 &&
          (statbuf.st_mode & S_IXUSR))
      {
         std::strncpy (progPath, shell, PATH_MAX - 1);
         progPath [PATH_MAX - 1] = '\0';
         return;
      }

      const passwd* entry = getpwuid (getuid ());
      shell = entry != nullptr ? entry->pw_shell : nullptr;
      if (shell != nullptr && stat (shell, &statbuf) == 0 &&
          (statbuf.st_mode & S_IXUSR))
      {
         std::strncpy (progPath, shell, PATH_MAX - 1);
         progPath [PATH_MAX - 1] = '\0';
         return;
      }
      std::strcpy (progPath, "/bin/sh");
   }

   void validateShell (char* progPath)
   {
      resolveShell (progPath);
      for (char* permitted = getusershell (); permitted != nullptr;
           permitted = getusershell ())
      {
         if (std::strcmp (progPath, permitted) == 0)
         {
            endusershell ();
            setenv ("SHELL", progPath, 1);
            return;
         }
      }
      endusershell ();
      unsetenv ("SHELL");
   }

   void setArgv0 (char* argv0)
   {
      const char* basename = std::strrchr (opts.shell, '/');
      basename = basename != nullptr ? basename + 1 : opts.shell;
      if (opts.login)
      {
         argv0 [0] = '-';
         std::strncpy (argv0 + 1, basename, PATH_MAX - 2);
         argv0 [PATH_MAX - 1] = '\0';
      }
      else
      {
         std::strncpy (argv0, basename, PATH_MAX - 1);
         argv0 [PATH_MAX - 1] = '\0';
      }
   }

   void childSignalHandler (int signal, siginfo_t* info, void*)
   {
      if (signal == SIGCHLD && info != nullptr)
         waitpid (info->si_pid, nullptr, WNOHANG);
   }

   void setupSignals ()
   {
      struct sigaction childAction {};
      childAction.sa_sigaction = childSignalHandler;
      childAction.sa_flags = SA_SIGINFO | SA_RESTART | SA_NOCLDSTOP;
      sigemptyset (&childAction.sa_mask);
      if (sigaction (SIGCHLD, &childAction, nullptr) < 0)
         SYS_ERROR ("can't install SIGCHLD handler: sigaction()");

      struct sigaction defaultAction {};
      defaultAction.sa_handler = SIG_DFL;
      sigemptyset (&defaultAction.sa_mask);
      if (sigaction (SIGINT, &defaultAction, nullptr) < 0)
         SYS_ERROR ("can't reset SIGINT handler: sigaction()");
      if (sigaction (SIGQUIT, &defaultAction, nullptr) < 0)
         SYS_ERROR ("can't reset SIGQUIT handler: sigaction()");
   }

   int startShell (const char* execPath, const char* const argv[])
   {
      int ptyFd = -1;
      const pid_t pid = zutty::pty_fork (ptyFd, opts.nCols, opts.nRows);
      if (pid < 0)
         SYS_ERROR ("fork");
      if (pid == 0)
      {
         if (setenv ("TERM", "xterm-256color", 1) < 0)
            SYS_ERROR ("setenv TERM");
         if (execvp (execPath, const_cast <char* const*> (argv)) < 0)
            SYS_ERROR ("execvp of ", execPath);
      }
      logT << "Shell subprocess started, pid: " << pid << std::endl;
      return ptyFd;
   }

   VtModifier convertModifiers (SDL_Keymod modifiers)
   {
      VtModifier result = VtModifier::none;
      if (modifiers & SDL_KMOD_SHIFT)
         result = result | VtModifier::shift;
      if (modifiers & SDL_KMOD_CTRL)
         result = result | VtModifier::control;
      if ((modifiers & SDL_KMOD_ALT) && !(modifiers & SDL_KMOD_MODE))
         result = result | VtModifier::alt;
      return result;
   }

   SDL_Keymod significantModifiers (SDL_Keymod modifiers)
   {
      return static_cast <SDL_Keymod> (
         modifiers & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT |
                      SDL_KMOD_MODE));
   }

   bool pasteSelection (bool primary)
   {
      char* text = primary ? SDL_GetPrimarySelectionText ()
                           : SDL_GetClipboardText ();
      if (text == nullptr)
         return false;
      vt->pasteSelection (text);
      SDL_free (text);
      return true;
   }

   bool copyPrimaryToClipboard ()
   {
      char* text = SDL_GetPrimarySelectionText ();
      if (text == nullptr)
         return false;
      const bool result = SDL_SetClipboardText (text);
      SDL_free (text);
      return result;
   }

   VtKey keypadKey (SDL_Scancode scancode, bool numLock)
   {
      using Key = VtKey;
      if (!numLock)
      {
         switch (scancode)
         {
         case SDL_SCANCODE_KP_0: return Key::KP_Insert;
         case SDL_SCANCODE_KP_1: return Key::KP_End;
         case SDL_SCANCODE_KP_2: return Key::KP_Down;
         case SDL_SCANCODE_KP_3: return Key::KP_PageDown;
         case SDL_SCANCODE_KP_4: return Key::KP_Left;
         case SDL_SCANCODE_KP_5: return Key::KP_Begin;
         case SDL_SCANCODE_KP_6: return Key::KP_Right;
         case SDL_SCANCODE_KP_7: return Key::KP_Home;
         case SDL_SCANCODE_KP_8: return Key::KP_Up;
         case SDL_SCANCODE_KP_9: return Key::KP_PageUp;
         case SDL_SCANCODE_KP_PERIOD: return Key::KP_Delete;
         default: break;
         }
      }

      switch (scancode)
      {
      case SDL_SCANCODE_KP_0: return Key::KP_0;
      case SDL_SCANCODE_KP_1: return Key::KP_1;
      case SDL_SCANCODE_KP_2: return Key::KP_2;
      case SDL_SCANCODE_KP_3: return Key::KP_3;
      case SDL_SCANCODE_KP_4: return Key::KP_4;
      case SDL_SCANCODE_KP_5: return Key::KP_5;
      case SDL_SCANCODE_KP_6: return Key::KP_6;
      case SDL_SCANCODE_KP_7: return Key::KP_7;
      case SDL_SCANCODE_KP_8: return Key::KP_8;
      case SDL_SCANCODE_KP_9: return Key::KP_9;
      case SDL_SCANCODE_KP_PERIOD: return Key::KP_Dot;
      case SDL_SCANCODE_KP_DIVIDE: return Key::KP_Slash;
      case SDL_SCANCODE_KP_MULTIPLY: return Key::KP_Star;
      case SDL_SCANCODE_KP_MINUS: return Key::KP_Minus;
      case SDL_SCANCODE_KP_PLUS: return Key::KP_Plus;
      case SDL_SCANCODE_KP_ENTER: return Key::KP_Enter;
      case SDL_SCANCODE_KP_EQUALS: return Key::KP_Equal;
      case SDL_SCANCODE_KP_COMMA: return Key::KP_Comma;
      case SDL_SCANCODE_KP_SPACE: return Key::KP_Space;
      case SDL_SCANCODE_KP_TAB: return Key::KP_Tab;
      default: return Key::NONE;
      }
   }

   VtKey specialKey (const SDL_KeyboardEvent& event)
   {
      using Key = VtKey;
      const Key keypad = keypadKey (
         event.scancode, (event.mod & SDL_KMOD_NUM) != 0);
      if (keypad != Key::NONE)
         return keypad;

      switch (event.key)
      {
      case SDLK_RETURN: return Key::Return;
      case SDLK_BACKSPACE: return Key::Backspace;
      case SDLK_TAB: return Key::Tab;
      case SDLK_INSERT: return Key::Insert;
      case SDLK_DELETE: return Key::Delete;
      case SDLK_HOME: return Key::Home;
      case SDLK_END: return Key::End;
      case SDLK_UP: return Key::Up;
      case SDLK_DOWN: return Key::Down;
      case SDLK_LEFT: return Key::Left;
      case SDLK_RIGHT: return Key::Right;
      case SDLK_PAGEUP: return Key::PageUp;
      case SDLK_PAGEDOWN: return Key::PageDown;
      case SDLK_F1: return Key::F1;
      case SDLK_F2: return Key::F2;
      case SDLK_F3: return Key::F3;
      case SDLK_F4: return Key::F4;
      case SDLK_F5: return Key::F5;
      case SDLK_F6: return Key::F6;
      case SDLK_F7: return Key::F7;
      case SDLK_F8: return Key::F8;
      case SDLK_F9: return Key::F9;
      case SDLK_F10: return Key::F10;
      case SDLK_F11: return Key::F11;
      case SDLK_F12: return Key::F12;
      case SDLK_F13: return Key::F13;
      case SDLK_F14: return Key::F14;
      case SDLK_F15: return Key::F15;
      case SDLK_F16: return Key::F16;
      case SDLK_F17: return Key::F17;
      case SDLK_F18: return Key::F18;
      case SDLK_F19: return Key::F19;
      case SDLK_F20: return Key::F20;
#ifdef DEBUG
      case SDLK_PRINTSCREEN: return Key::Print;
#endif
      default: return Key::NONE;
      }
   }

   bool controlCharacter (SDL_Keycode key, uint8_t& character)
   {
      if (key >= SDLK_A && key <= SDLK_Z)
      {
         character = static_cast <uint8_t> (key - SDLK_A + 1);
         return true;
      }
      switch (key)
      {
      case SDLK_SPACE: case SDLK_AT: character = 0; return true;
      case SDLK_2: character = 0; return true;
      case SDLK_3: character = 27; return true;
      case SDLK_4: character = 28; return true;
      case SDLK_5: character = 29; return true;
      case SDLK_6: character = 30; return true;
      case SDLK_7: character = 31; return true;
      case SDLK_8: character = 127; return true;
      case SDLK_LEFTBRACKET: character = 27; return true;
      case SDLK_BACKSLASH: character = 28; return true;
      case SDLK_RIGHTBRACKET: character = 29; return true;
      case SDLK_CARET: character = 30; return true;
      case SDLK_UNDERSCORE: character = 31; return true;
      case SDLK_SLASH: character = 31; return true;
      case SDLK_QUESTION: character = 127; return true;
      default:
         if (key > 0 && key < 128)
         {
            character = static_cast <uint8_t> (key);
            return true;
         }
         return false;
      }
   }

   void onKeyDown (const SDL_KeyboardEvent& event)
   {
      const SDL_Keymod rawModifiers = significantModifiers (event.mod);
      const VtModifier modifiers = convertModifiers (rawModifiers);

      if (event.key == SDLK_PAGEUP && modifiers == VtModifier::shift)
      {
         vt->pageUp ();
         return;
      }
      if (event.key == SDLK_PAGEDOWN && modifiers == VtModifier::shift)
      {
         vt->pageDown ();
         return;
      }
      if (event.key == SDLK_C && modifiers == VtModifier::shift_control)
      {
         copyPrimaryToClipboard ();
         return;
      }
      if (event.key == SDLK_V && modifiers == VtModifier::shift_control)
      {
         pasteSelection (false);
         return;
      }
      if ((event.key == SDLK_INSERT ||
           event.scancode == SDL_SCANCODE_KP_0) &&
          modifiers == VtModifier::shift)
      {
         pasteSelection (true);
         return;
      }
      if (event.key == SDLK_SPACE && mouseContext.selectionOngoing)
      {
         vt->selectRectangularModeToggle ();
         return;
      }

      if (event.key == SDLK_ESCAPE)
      {
         vt->writePty (static_cast <uint8_t> ('\x1b'), modifiers, true);
         return;
      }

      const VtKey key = specialKey (event);
      if (key != VtKey::NONE)
      {
         vt->writePty (key, modifiers, true);
         return;
      }

      if (rawModifiers & SDL_KMOD_CTRL)
      {
         uint8_t character = 0;
         if (controlCharacter (event.key, character))
            vt->writePty (character, modifiers, true);
      }
   }

   void onTextInput (const char* text)
   {
      if (text == nullptr || text [0] == '\0')
         return;
      const SDL_Keymod rawModifiers = significantModifiers (SDL_GetModState ());
      const VtModifier modifiers = convertModifiers (rawModifiers);
      const size_t length = std::strlen (text);

      if (length == 1 && static_cast <uint8_t> (text [0]) < 0x80)
      {
         vt->writePty (static_cast <uint8_t> (text [0]), modifiers, true);
         return;
      }

      if ((rawModifiers & SDL_KMOD_ALT) && !(rawModifiers & SDL_KMOD_MODE) &&
          opts.altSendsEscape)
         vt->writePty ("\x1b", true);
      vt->writePty (text, true);
   }

   int toPixelX (float x)
   {
      return static_cast <int> (std::lround (
         x * std::max (1.0f, SDL_GetWindowPixelDensity (window))));
   }

   int toPixelY (float y)
   {
      return static_cast <int> (std::lround (
         y * std::max (1.0f, SDL_GetWindowPixelDensity (window))));
   }

   bool isMouseProtocol (SDL_Keymod modifiers,
                         const MouseTrackingState& tracking)
   {
      return !mouseContext.selectionOngoing &&
             !(modifiers & SDL_KMOD_SHIFT) &&
             tracking.mode != MouseTrackingMode::Disabled;
   }

   void mouseProtocolCoordinates (int pixelX, int pixelY,
                                  uint16_t& column, uint16_t& row)
   {
      column = std::max (0, (pixelX - opts.border - 1) /
                            fontpk->getPx ()) + 1;
      row = std::max (0, (pixelY - opts.border - 1) /
                         fontpk->getPy ()) + 1;
   }

   void mouseProtocolSend (MouseTrackingEnc encoding, MouseEventType type,
                           SDL_Keymod modifiers,
                           SDL_MouseButtonFlags buttonState,
                           int button, int column, int row)
   {
      int code = 0;
      if (type == MouseEventType::Motion)
      {
         if (buttonState & SDL_BUTTON_LMASK) code = 32;
         else if (buttonState & SDL_BUTTON_MMASK) code = 33;
         else if (buttonState & SDL_BUTTON_RMASK) code = 34;
         else code = 35;
      }
      else if (type == MouseEventType::Release &&
               encoding != MouseTrackingEnc::SGR)
         code = 3;
      else
      {
         switch (button)
         {
         case 1: code = 0; break;
         case 2: code = 1; break;
         case 3: code = 2; break;
         case 4: code = 64; break;
         case 5: code = 65; break;
         case 6: code = 66; break;
         case 7: code = 67; break;
         case 8: code = 128; break;
         case 9: code = 129; break;
         default: return;
         }
      }

      if (modifiers & SDL_KMOD_SHIFT) code += 4;
      if ((modifiers & SDL_KMOD_ALT) && !(modifiers & SDL_KMOD_MODE)) code += 8;
      if (modifiers & SDL_KMOD_CTRL) code += 16;

      std::ostringstream output;
      switch (encoding)
      {
      case MouseTrackingEnc::Default:
         output << "\x1b[M" << static_cast <char> (32 + code)
                << static_cast <char> (32 + column)
                << static_cast <char> (32 + row);
         break;
      case MouseTrackingEnc::UTF8:
         output << "\x1b[M";
         zutty::Utf8Encoder::pushUnicode (
            32 + code, [&output] (char ch) { output << ch; });
         zutty::Utf8Encoder::pushUnicode (
            32 + column, [&output] (char ch) { output << ch; });
         zutty::Utf8Encoder::pushUnicode (
            32 + row, [&output] (char ch) { output << ch; });
         break;
      case MouseTrackingEnc::SGR:
         output << "\x1b[<" << code << ';' << column << ';' << row
                << (type == MouseEventType::Release ? 'm' : 'M');
         break;
      case MouseTrackingEnc::URXVT:
         output << "\x1b[" << code + 32 << ';' << column << ';' << row << 'M';
         break;
      }
      vt->writePty (output.str ().c_str ());
   }

   void sendMouseButtonProtocol (MouseEventType type, int button,
                                 int pixelX, int pixelY,
                                 SDL_Keymod modifiers,
                                 SDL_MouseButtonFlags buttonState,
                                 const MouseTrackingState& tracking)
   {
      if (button > 11 ||
          (type == MouseEventType::Release && button > 3))
         return;
      if (tracking.mode == MouseTrackingMode::Disabled ||
          (type == MouseEventType::Release &&
           tracking.mode == MouseTrackingMode::X10_Compat))
         return;

      uint16_t column = 0;
      uint16_t row = 0;
      mouseProtocolCoordinates (pixelX, pixelY, column, row);
      mouseProtocolSend (
         tracking.enc, type,
         tracking.mode == MouseTrackingMode::X10_Compat
            ? static_cast <SDL_Keymod> (0)
            : modifiers,
         buttonState, button, column, row);
   }

   int terminalButton (Uint8 sdlButton)
   {
      switch (sdlButton)
      {
      case SDL_BUTTON_LEFT: return 1;
      case SDL_BUTTON_MIDDLE: return 2;
      case SDL_BUTTON_RIGHT: return 3;
      case SDL_BUTTON_X1: return 8;
      case SDL_BUTTON_X2: return 9;
      default: return 0;
      }
   }

   void onMouseButton (const SDL_MouseButtonEvent& event, bool pressed,
                       bool& releasePtyHold)
   {
      const int pixelX = toPixelX (event.x);
      const int pixelY = toPixelY (event.y);
      const SDL_Keymod modifiers = SDL_GetModState ();
      const SDL_MouseButtonFlags buttons = SDL_GetMouseState (nullptr, nullptr);
      const auto& tracking = vt->getMouseTrackingState ();
      const int protocolButton = terminalButton (event.button);

      if (isMouseProtocol (modifiers, tracking))
      {
         sendMouseButtonProtocol (
            pressed ? MouseEventType::Press : MouseEventType::Release,
            protocolButton, pixelX, pixelY, modifiers, buttons, tracking);
         return;
      }

      if (pressed)
      {
         const bool cycleSnapTo = event.clicks > 1;
         if (event.button == SDL_BUTTON_LEFT)
         {
            vt->selectStart (pixelX, pixelY, cycleSnapTo);
            mouseContext.selectionOngoing = true;
         }
         else if (event.button == SDL_BUTTON_RIGHT)
         {
            vt->selectExtend (pixelX, pixelY, cycleSnapTo);
            mouseContext.selectionOngoing = true;
         }
         return;
      }

      if (event.button == SDL_BUTTON_LEFT ||
          event.button == SDL_BUTTON_RIGHT)
      {
         std::string selection;
         mouseContext.selectionOngoing = false;
         releasePtyHold = true;
         if (vt->selectFinish (selection))
         {
            if (!SDL_SetPrimarySelectionText (selection.c_str ()))
            {
               logW << "Could not set Wayland primary selection: "
                    << SDL_GetError () << std::endl;
            }
            if (opts.autoCopyMode &&
                !SDL_SetClipboardText (selection.c_str ()))
            {
               logW << "Could not set clipboard: "
                    << SDL_GetError () << std::endl;
            }
         }
      }
      else if (event.button == SDL_BUTTON_MIDDLE)
         pasteSelection (true);
   }

   void onMouseMotion (const SDL_MouseMotionEvent& event)
   {
      const int pixelX = toPixelX (event.x);
      const int pixelY = toPixelY (event.y);
      const SDL_Keymod modifiers = SDL_GetModState ();
      const auto& tracking = vt->getMouseTrackingState ();
      if (isMouseProtocol (modifiers, tracking))
      {
         if (tracking.mode == MouseTrackingMode::VT200_ButtonEvent &&
             !(event.state & (SDL_BUTTON_LMASK | SDL_BUTTON_MMASK |
                              SDL_BUTTON_RMASK)))
            return;
         if (tracking.mode != MouseTrackingMode::VT200_ButtonEvent &&
             tracking.mode != MouseTrackingMode::VT200_AnyEvent)
            return;

         static uint16_t lastColumn = UINT16_MAX;
         static uint16_t lastRow = UINT16_MAX;
         uint16_t column = 0;
         uint16_t row = 0;
         mouseProtocolCoordinates (pixelX, pixelY, column, row);
         if (column != lastColumn || row != lastRow)
         {
            mouseProtocolSend (tracking.enc, MouseEventType::Motion,
                               modifiers, event.state, 0, column, row);
            lastColumn = column;
            lastRow = row;
         }
      }
      else if (event.state & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK))
         vt->selectUpdate (pixelX, pixelY);
   }

   void onMouseWheel (const SDL_MouseWheelEvent& event)
   {
      float wheelX = event.x;
      float wheelY = event.y;
      if (event.direction == SDL_MOUSEWHEEL_FLIPPED)
      {
         wheelX = -wheelX;
         wheelY = -wheelY;
      }

      const SDL_Keymod modifiers = SDL_GetModState ();
      const auto& tracking = vt->getMouseTrackingState ();
      if (isMouseProtocol (modifiers, tracking))
      {
         const int pixelX = toPixelX (event.mouse_x);
         const int pixelY = toPixelY (event.mouse_y);
         const SDL_MouseButtonFlags buttons =
            SDL_GetMouseState (nullptr, nullptr);
         if (wheelY > 0)
            sendMouseButtonProtocol (MouseEventType::Press, 4,
                                     pixelX, pixelY, modifiers,
                                     buttons, tracking);
         else if (wheelY < 0)
            sendMouseButtonProtocol (MouseEventType::Press, 5,
                                     pixelX, pixelY, modifiers,
                                     buttons, tracking);
         if (wheelX < 0)
            sendMouseButtonProtocol (MouseEventType::Press, 6,
                                     pixelX, pixelY, modifiers,
                                     buttons, tracking);
         else if (wheelX > 0)
            sendMouseButtonProtocol (MouseEventType::Press, 7,
                                     pixelX, pixelY, modifiers,
                                     buttons, tracking);
      }
      else if (wheelY > 0)
         vt->mouseWheelUp ();
      else if (wheelY < 0)
         vt->mouseWheelDown ();
   }

   std::string getSelectionForOsc (bool primary)
   {
      char* text = primary ? SDL_GetPrimarySelectionText ()
                           : SDL_GetClipboardText ();
      if (text == nullptr)
         return {};
      std::string result (text);
      SDL_free (text);
      return result;
   }

   // Convert an OSC 7 argument (a file:// URL, possibly with a hostname
   // and percent-encoded characters, or a plain absolute path) to a path.
   // Returns an empty string if the argument cannot be parsed.
   std::string oscCwdToPath (const std::string& argument)
   {
      constexpr const char scheme [] = "file://";
      constexpr const size_t schemeLen = sizeof (scheme) - 1;

      std::string url = argument;
      if (url.compare (0, schemeLen, scheme) == 0)
      {
         const size_t pathStart = url.find ('/', schemeLen);
         if (pathStart == std::string::npos)
            return {};
         url = url.substr (pathStart);
      }
      if (url.empty () || url [0] != '/')
         return {};

      std::string path;
      path.reserve (url.size ());
      for (size_t k = 0; k < url.size (); ++k)
      {
         if (url [k] == '%' && k + 2 < url.size () &&
             isxdigit (static_cast <unsigned char> (url [k + 1])) &&
             isxdigit (static_cast <unsigned char> (url [k + 2])))
         {
            path.push_back (static_cast <char> (
               std::stoi (url.substr (k + 1, 2), nullptr, 16)));
            k += 2;
         }
         else
            path.push_back (url [k]);
      }
      return path;
   }

   bool appTitleSet = false;

   void handleOsc (int command, const std::string& argument)
   {
      switch (command)
      {
      case 0:
      case 1:
      case 2:
         // Resetting the title to the default (e.g. on RIS) re-enables
         // OSC 7 driven titles.
         appTitleSet = argument != opts.title;
         SDL_SetWindowTitle (window, argument.c_str ());
         return;
      case 7:
      {
         // Shell reports its current working directory; reflect it in the
         // window title unless the application has set an explicit title.
         const std::string cwd = oscCwdToPath (argument);
         if (cwd.empty ())
            logW << "OSC 7: cannot parse '" << argument << "'" << std::endl;
         else if (! appTitleSet)
            SDL_SetWindowTitle (window, cwd.c_str ());
         return;
      }
      case 52:
         break;
      default:
         logU << "unhandled OSC: '" << command << ';' << argument << "'"
              << std::endl;
         return;
      }

      const size_t separator = argument.find (';');
      if (separator == std::string::npos)
      {
         logW << "Malformed OSC 52 argument" << std::endl;
         return;
      }
      const std::string selectors = argument.substr (0, separator);
      const std::string payload = argument.substr (separator + 1);
      bool primary = selectors.empty () || selectors.find ('s') != std::string::npos ||
                     selectors.find ('p') != std::string::npos;
      bool clipboard = selectors.empty () || selectors.find ('s') != std::string::npos ||
                       selectors.find ('c') != std::string::npos;

      if (payload == "?")
      {
         std::string content;
         if (primary)
            content = getSelectionForOsc (true);
         if (content.empty () && clipboard)
            content = getSelectionForOsc (false);
         const std::string reply = "\x1b]52;;" +
            zutty::base64::encode (content) + "\x1b\\";
         vt->writePty (reply.c_str ());
         return;
      }

      const std::string content = zutty::base64::decode (payload);
      if (primary && !SDL_SetPrimarySelectionText (content.c_str ()))
      {
         logW << "OSC 52: could not set primary selection: "
              << SDL_GetError () << std::endl;
      }
      if (clipboard && !SDL_SetClipboardText (content.c_str ()))
      {
         logW << "OSC 52: could not set clipboard: "
              << SDL_GetError () << std::endl;
      }
   }

   bool eventLoop (PtyEventSource& ptySource, Uint32 ptyEventType)
   {
      bool ptyPending = false;
      while (true)
      {
         SDL_Event event {};
         if (!SDL_WaitEvent (&event))
            throw std::runtime_error (
               std::string ("SDL_WaitEvent failed: ") + SDL_GetError ());

         bool releasePtyHold = false;
         if (event.type == ptyEventType)
         {
            if (mouseContext.selectionOngoing)
               ptyPending = true;
            else
            {
               const bool finished = vt->readPty ();
               ptySource.acknowledge ();
               if (finished)
                  return false;
            }
         }
         else switch (event.type)
         {
         case SDL_EVENT_QUIT:
         case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return true;
         case SDL_EVENT_WINDOW_EXPOSED:
            vt->redraw ();
            break;
         case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (event.window.data1 > 0 && event.window.data2 > 0)
            {
               const int width = std::min (event.window.data1,
                                           static_cast <int> (UINT16_MAX));
               const int height = std::min (event.window.data2,
                                            static_cast <int> (UINT16_MAX));
               vt->resize (width, height);
               vt->redraw ();
            }
            break;
         case SDL_EVENT_WINDOW_FOCUS_GAINED:
            if (vt->getMouseTrackingState ().focusEventMode)
               vt->writePty ("\x1b[I");
            vt->setHasFocus (true);
            break;
         case SDL_EVENT_WINDOW_FOCUS_LOST:
            if (vt->getMouseTrackingState ().focusEventMode)
               vt->writePty ("\x1b[O");
            vt->setHasFocus (false);
            break;
         case SDL_EVENT_KEY_DOWN:
            onKeyDown (event.key);
            break;
         case SDL_EVENT_TEXT_INPUT:
            onTextInput (event.text.text);
            break;
         case SDL_EVENT_MOUSE_BUTTON_DOWN:
            onMouseButton (event.button, true, releasePtyHold);
            break;
         case SDL_EVENT_MOUSE_BUTTON_UP:
            onMouseButton (event.button, false, releasePtyHold);
            break;
         case SDL_EVENT_MOUSE_MOTION:
            onMouseMotion (event.motion);
            break;
         case SDL_EVENT_MOUSE_WHEEL:
            onMouseWheel (event.wheel);
            break;
         default:
            break;
         }

         if (releasePtyHold && ptyPending)
         {
            ptyPending = false;
            const bool finished = vt->readPty ();
            ptySource.acknowledge ();
            if (finished)
               return false;
         }
      }
   }

   void checkLocale ()
   {
      const char* locale = setlocale (LC_ALL, "");
      if (locale == nullptr)
      {
         std::cout << "Warning: could not set locale; international input "
                      "may be broken.\n";
         return;
      }
      if (std::strcmp (nl_langinfo (CODESET), "UTF-8") != 0)
         std::cout << "Warning: non-UTF-8 locale " << locale
                   << "; international input may be broken.\n";
   }

   int run (int argc, char* argv[])
   {
      checkLocale ();
      opts.initialize (&argc, argv);
      opts.parse ();
      if (opts.verbose)
         opts.printVersion ();
      if (setenv ("ZUTTY_VERSION", ZUTTY_VERSION, 1) < 0)
         SYS_ERROR ("setenv ZUTTY_VERSION");

      char argv0 [PATH_MAX] {};
      char progPath [PATH_MAX] {};
      char* defaultShellArgv [] = {argv0, nullptr};
      char** shellArgv = defaultShellArgv;
      if (argc > 2 && std::strcmp (argv [1], "-e") == 0)
      {
         shellArgv = argv + 2;
         if (opts.titleSource != zutty::OptionSource::CmdLine)
            opts.title = argv [2];
         std::strncpy (progPath, argv [2], PATH_MAX - 1);
      }
      else if (argc == 2)
      {
         setArgv0 (argv0);
         std::strncpy (progPath, argv [1], PATH_MAX - 1);
         validateShell (progPath);
      }
      else
      {
         setArgv0 (argv0);
         std::strncpy (progPath, opts.shell, PATH_MAX - 1);
         validateShell (progPath);
      }

      SDL_SetAppMetadata ("Zutty", ZUTTY_VERSION, "org.zutty.Zutty");
      SDL_SetHint (SDL_HINT_VIDEO_DRIVER, "wayland");
      if (!SDL_Init (SDL_INIT_VIDEO | SDL_INIT_EVENTS))
         throw std::runtime_error (
            std::string ("SDL_Init failed: ") + SDL_GetError ());
      const char* driver = SDL_GetCurrentVideoDriver ();
      if (driver == nullptr || std::strcmp (driver, "wayland") != 0)
         throw std::runtime_error (
            std::string ("Wayland SDL driver required, got: ") +
            (driver != nullptr ? driver : "none"));

      const int initialWidth = std::max (
         320, static_cast <int> (opts.nCols) * opts.fontsize / 2);
      const int initialHeight = std::max (
         200, static_cast <int> (opts.nRows) * opts.fontsize);
      window = SDL_CreateWindow (
         opts.title, initialWidth, initialHeight,
         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
         SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
      if (window == nullptr)
         throw std::runtime_error (
            std::string ("SDL_CreateWindow failed: ") + SDL_GetError ());
      if (!SDL_ShowWindow (window) || !SDL_SyncWindow (window))
         throw std::runtime_error (
            std::string ("Could not map Wayland window: ") + SDL_GetError ());

      const float density = std::max (
         1.0f, SDL_GetWindowPixelDensity (window));
      opts.fontsize = static_cast <uint8_t> (std::clamp (
         static_cast <int> (std::lround (opts.fontsize * density)), 1, 255));
      opts.border = static_cast <uint16_t> (std::clamp (
         static_cast <int> (std::lround (opts.border * density)), 0, 3000));

      fontpk = std::make_unique <Fontpack> (
         opts.fontpath, opts.fontname, opts.dwfontname);
      const int desiredPixelWidth =
         2 * opts.border + opts.nCols * fontpk->getPx ();
      const int desiredPixelHeight =
         2 * opts.border + opts.nRows * fontpk->getPy ();
      const int desiredWidth = std::max (
         1, static_cast <int> (std::ceil (desiredPixelWidth / density)));
      const int desiredHeight = std::max (
         1, static_cast <int> (std::ceil (desiredPixelHeight / density)));
      SDL_SetWindowMinimumSize (
         window,
         std::max (1, static_cast <int> (std::ceil (
            (2 * opts.border + fontpk->getPx ()) / density))),
         std::max (1, static_cast <int> (std::ceil (
            (2 * opts.border + fontpk->getPy ()) / density))));
      if (!SDL_SetWindowSize (window, desiredWidth, desiredHeight) ||
          !SDL_SyncWindow (window))
         throw std::runtime_error (
            std::string ("Could not size Wayland window: ") + SDL_GetError ());

      int pixelWidth = 0;
      int pixelHeight = 0;
      if (!SDL_GetWindowSizeInPixels (window, &pixelWidth, &pixelHeight))
         throw std::runtime_error (
            std::string ("SDL_GetWindowSizeInPixels failed: ") +
            SDL_GetError ());
      if (pixelWidth > UINT16_MAX || pixelHeight > UINT16_MAX)
         throw std::runtime_error ("Initial window exceeds terminal limits");

      SDL_Cursor* cursor = SDL_CreateSystemCursor (SDL_SYSTEM_CURSOR_TEXT);
      if (cursor != nullptr)
         SDL_SetCursor (cursor);
      if (!SDL_StartTextInput (window))
      {
         logW << "SDL_StartTextInput failed: " << SDL_GetError ()
              << std::endl;
      }

      renderer = std::make_unique <Renderer> (window, fontpk.get ());
      setupSignals ();
      const int ptyFd = startShell (progPath, shellArgv);
      vt = std::make_unique <Vterm> (
         fontpk->getPx (), fontpk->getPy (), pixelWidth, pixelHeight, ptyFd);
      vt->setRefreshHandler (
         [] (const zutty::Frame& frame) { renderer->update (frame); });
      vt->setOscHandler (
         [] (int command, const std::string& argument)
         { handleOsc (command, argument); });
      vt->setBellHandler (
         [] () { SDL_FlashWindow (window, SDL_FLASH_BRIEFLY); });
      vt->resize (pixelWidth, pixelHeight);
      vt->redraw ();

      const Uint32 ptyEventType = SDL_RegisterEvents (1);
      if (ptyEventType == static_cast <Uint32> (-1))
         throw std::runtime_error (
            std::string ("SDL_RegisterEvents failed: ") + SDL_GetError ());

      bool windowClosed = false;
      {
         PtyEventSource ptySource (ptyFd, ptyEventType);
         windowClosed = eventLoop (ptySource, ptyEventType);
      }

      vt.reset ();
      close (ptyFd);
      renderer.reset ();
      fontpk.reset ();
      if (cursor != nullptr)
         SDL_DestroyCursor (cursor);
      SDL_DestroyWindow (window);
      window = nullptr;
      SDL_Quit ();
      return windowClosed ? 0 : 0;
   }

   void emergencyCleanup ()
   {
      vt.reset ();
      renderer.reset ();
      fontpk.reset ();
      if (window != nullptr)
      {
         SDL_DestroyWindow (window);
         window = nullptr;
      }
      SDL_Quit ();
   }
}

int main (int argc, char* argv[])
{
   try
   {
      return run (argc, argv);
   }
   catch (const std::exception& error)
   {
      emergencyCleanup ();
      std::cerr << "Error: " << error.what () << std::endl;
      return 1;
   }
}
