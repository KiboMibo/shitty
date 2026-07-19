{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  packages = with pkgs; [
    fontconfig
    freetype
    glslang
    glfw
    pkg-config
    python3
    vulkan-loader
  ];
}
