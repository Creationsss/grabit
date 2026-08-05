{
  description = "screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    utils,
  }:
    utils.lib.eachDefaultSystem (system: let
      pkgs = import nixpkgs {inherit system;};

      version = let
        lines = pkgs.lib.splitString "\n" (builtins.readFile ./Makefile);
        hits =
          builtins.filter (m: m != null)
          (map (builtins.match "VERSION[[:space:]]*:=[[:space:]]*([^[:space:]]+)[[:space:]]*") lines);
      in
        builtins.head (builtins.head hits);

      gitCommit =
        if self ? rev
        then builtins.substring 0 8 self.rev
        else if self ? dirtyRev
        then "${builtins.substring 0 8 self.dirtyRev}+"
        else "";

      runtimeDeps = with pkgs; [
        ffmpeg-headless
        tesseract
        translate-shell
      ];

      mkGrabit = {wrapped ? true}:
        pkgs.stdenv.mkDerivation {
          pname =
            if wrapped
            then "grabit"
            else "grabit-minimal";
          inherit version;

          src = ./.;

          makeFlags = [
            "PREFIX=$(out)"
            "GIT_COMMIT=${gitCommit}"
          ];

          nativeBuildInputs = with pkgs;
            [
              pkg-config
              wayland-scanner
            ]
            ++ pkgs.lib.optional wrapped pkgs.makeWrapper;

          buildInputs = with pkgs; [
            json_c
            curl
            file
            wayland
            cairo
            libxkbcommon
            dbus
            libjpeg
            libwebp
          ];

          postFixup = pkgs.lib.optionalString wrapped ''
            wrapProgram $out/bin/grabit \
              --prefix PATH : ${pkgs.lib.makeBinPath runtimeDeps}
          '';

          meta = with pkgs.lib; {
            description = "screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.";
            homepage = "https://heliopolis.live/creations/grabit";
            license = licenses.agpl3Plus;
            platforms = platforms.linux;
            mainProgram = "grabit";
          };
        };
    in {
      packages.default = mkGrabit {};
      packages.minimal = mkGrabit {wrapped = false;};

      devShells.default = pkgs.mkShell {
        inputsFrom = [self.packages.${system}.default];
        buildInputs = with pkgs; [
          clang-tools
          bear
          gdb
        ];
      };
    });
}
