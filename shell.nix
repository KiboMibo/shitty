{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  packages = with pkgs; [
    freetype
    glslang
    meson
    ninja
    pkg-config
    python3
    sdl3
    vulkan-loader
  ];
}
