{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { nixpkgs, ... }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      devShells.${system}.default = pkgs.mkShell.override {
        stdenv = pkgs.clangStdenv;
      } {
        nativeBuildInputs = with pkgs; [
          clang-tools
          bear
          sqlite
          pkg-config
          qt6.wrapQtAppsHook
        ];
        buildInputs = with pkgs; [
          socat
          sqlite
          qt6.qtbase
          qt6.qtwayland
        ];
      };
    };
}
