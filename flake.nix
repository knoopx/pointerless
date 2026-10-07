{
  description = "pointerless development shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      lib = nixpkgs.lib;
    in
    {
      packages = lib.genAttrs systems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          pointerless = pkgs.stdenv.mkDerivation ({
            pname = "pointerless";
            version = "0.4.1"; # keep in sync with meson.build project()
            # Drop build artifacts: NCNN comes from nixpkgs so meson needs no
            # vendored copy.
            # Chained on lib.cleanSource so the default VCS/backup filter
            # (which also drops .jj) stays in effect.
            src = lib.cleanSourceWith {
              src = lib.cleanSource ./.;
              filter = name: type:
                type != "directory"
                || !lib.elem (lib.baseNameOf name) [ "build" "builddbg" ];
            };
            nativeBuildInputs = [ pkgs.meson pkgs.ninja pkgs.pkg-config ];
            # Absolute datadir so GUIDETECT_MODEL_DIR (datadir/pointerless,
            # baked in at compile time) resolves to $out/share/pointerless
            # and the installed binary finds the model from any CWD.
            # Only datadir is overridden here: prefix is already passed by
            # stdenv's mesonConfigurePhase as --prefix, and passing it again
            # as -Dprefix makes meson fail with "Got argument prefix as both
            # -Dprefix and --prefix".
            mesonFlagsArray = [
              "-Ddatadir=${placeholder "out"}/share"
            ];
            buildInputs = [
              pkgs.wayland
              pkgs."wayland-protocols"
              pkgs.wayland-scanner.dev
              pkgs.libxkbcommon
              pkgs.cairo
              pkgs.ncnn
            ];
            meta = {
              description = "Move the mouse pointer with the keyboard on Wayland";
              mainProgram = "pointerless";
              license = lib.licenses.gpl3Only;
            };
          });
        in
        { inherit pointerless; default = pointerless; });
      devShell = nixpkgs.lib.genAttrs systems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          # Dev outputs that carry the .pc files meson looks up; ncnn is the
          # nixpkgs package meson requires as a system dependency.
          devDeps = [
            pkgs.wayland.dev
            pkgs.wayland-scanner.dev
            pkgs.wayland-protocols
            pkgs.libxkbcommon.dev
            pkgs.cairo.dev
            pkgs.ncnn
          ];
          # A plain mkShell does not reliably propagate PKG_CONFIG_PATH for
          # these .dev outputs, so assemble it explicitly from each dep's
          # pkgconfig search dirs (pkg-config tolerates dirs that are absent).
          pkgConfigPath = nixpkgs.lib.concatStringsSep ":" (
            nixpkgs.lib.flatten (
              map (d: [ ((toString d) + "/lib/pkgconfig") ((toString d) + "/share/pkgconfig") ]) devDeps
            ));
        in
        pkgs.mkShell {
          packages = [
            pkgs.meson
            pkgs.ninja
            pkgs.gcc
            pkgs.gcc.cc
          ] ++ devDeps;
          shellHook = ''
            export PKG_CONFIG_PATH="${pkgConfigPath}":"$PKG_CONFIG_PATH"
          '';
        });
    };
}
