{
  description = "Niri-native screenshot and annotation overlay";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;
      supportedSystems = [ "x86_64-linux" ];
      forAllSystems = lib.genAttrs supportedSystems;
      packageFor =
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          runtimePackages = with pkgs; [
            grim
            libnotify
            tesseract
            wl-clipboard
          ];
          layerShellPluginDir = "${pkgs.kdePackages.layer-shell-qt}/lib/qt-6/plugins";
        in
        pkgs.stdenv.mkDerivation {
          pname = "omasnap";
          version = "1.0.6";
          src = self;

          strictDeps = true;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            kdePackages.layer-shell-qt
            libglvnd
            qt6.qtbase
          ];

          nativeCheckInputs = [ pkgs.tesseract ];

          cmakeFlags = [ "-DCMAKE_BUILD_TYPE=Release" ];
          doCheck = true;

          checkPhase = ''
            runHook preCheck
            export HOME="$TMPDIR"
            export XDG_CACHE_HOME="$TMPDIR/cache"
            export XDG_RUNTIME_DIR="$TMPDIR/runtime"
            export OMASNAP_DISABLE_NOTIFICATIONS=1
            export FONTCONFIG_FILE="${pkgs.fontconfig.out}/etc/fonts/fonts.conf"
            mkdir -p "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR"
            chmod 700 "$XDG_RUNTIME_DIR"
            QT_QPA_PLATFORM=offscreen ./omasnap-smoke "$TMPDIR/omasnap-smoke"
            ./omasnap-signal-smoke int
            ./omasnap-signal-smoke term
            ./omasnap-cli-smoke
            cp "$TMPDIR/omasnap-smoke-file-mode.png" "$TMPDIR/image with spaces.png"
            QT_QPA_PLATFORM=offscreen ./omasnap --saved-notification "$TMPDIR/image with spaces.png"
            if QT_QPA_PLATFORM=offscreen ./omasnap --file "$TMPDIR/missing.png"; then
              echo "missing --file target unexpectedly succeeded" >&2
              exit 1
            fi
            printf 'not an image\n' > "$TMPDIR/invalid image.png"
            if QT_QPA_PLATFORM=offscreen ./omasnap "$TMPDIR/invalid image.png"; then
              echo "invalid positional image unexpectedly succeeded" >&2
              exit 1
            fi
            runHook postCheck
          '';

          qtWrapperArgs = [
            "--prefix PATH : ${lib.makeBinPath runtimePackages}"
          ];

          postFixup = ''
            test -e "${layerShellPluginDir}/wayland-shell-integration/liblayer-shell.so"
            grep -aFq "${layerShellPluginDir}" "$out/bin/omasnap"
            QT_QPA_PLATFORM=offscreen "$out/bin/omasnap" --version
          '';

          meta = {
            description = "Niri-native screenshot and annotation overlay";
            homepage = "https://github.com/Kabilan108/omasnap";
            license = lib.licenses.mit;
            mainProgram = "omasnap";
            platforms = lib.platforms.linux;
          };
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          package = packageFor system;
        in
        {
          default = package;
          omasnap = package;
        }
      );

      apps = forAllSystems (
        system:
        let
          app = {
            type = "app";
            program = lib.getExe self.packages.${system}.omasnap;
            meta = self.packages.${system}.omasnap.meta;
          };
        in
        {
          default = app;
          omasnap = app;
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.omasnap ];
            packages = with pkgs; [
              clang-tools
              grim
              libnotify
              nixfmt
              shellcheck
              tesseract
              wl-clipboard
            ];
          };
        }
      );

      checks = forAllSystems (system: {
        inherit (self.packages.${system}) omasnap;
      });
    };
}
