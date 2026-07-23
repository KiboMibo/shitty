# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

{ pkgs ? import <nixpkgs> {} }:

(pkgs.mkShell.override { stdenv = pkgs.llvmPackages.stdenv; }) {
  FONTCONFIG_FILE = pkgs.makeFontsConf {
    fontDirectories = [ pkgs.dejavu_fonts pkgs.ibm-plex ];
  };

  packages = with pkgs; [
    brotli
    fontconfig
    freetype
    glslang
    glfw
    pkg-config
    python3
    utf8proc
    vulkan-loader
  ];
}
