{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  packages = with pkgs; [
    fontconfig
    freetype
    glslang
    glfw
    meson
    ninja
    pkg-config
    python3
    vulkan-loader
  ];
}
