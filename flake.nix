{
  description = "HyprFM - a lightweight Qt6/QML file manager for Hyprland";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # Mirrors the two git submodules pulled in by PKGBUILD's `git submodule
    # update` (src/qml/icons, src/qml/Quill). Plain flake sources don't fetch
    # submodules, so they're pinned here instead and copied into place in
    # postPatch. Pinned to an exact commit rather than a branch: the commits
    # hyprfm's .gitmodules reference aren't always reachable from quill(-icons)'s
    # default branch, so tracking the branch can silently resolve to an older
    # commit. Keep these revs in sync with `git ls-tree main src/qml/Quill
    # src/qml/icons` whenever the submodules are bumped.
    quill-icons = {
      url = "github:soyeb-jim285/quill-icons/10db5facf6a560e60d2693ccd1909267ef436002";
      flake = false;
    };
    quill = {
      url = "github:soyeb-jim285/quill/ad1785f108ea47b8950cbebcd6dd557ce84e4879";
      flake = false;
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      quill-icons,
      quill,
    }:
    let
      # aarch64 evaluates cleanly -- every dependency resolves for it, which
      # rules out the usual "not available on this platform" failure -- but it
      # has never actually been compiled or run on aarch64 hardware. Treat it
      # as offered, not verified.
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forEachSystem = nixpkgs.lib.genAttrs systems;
      pkgsFor = system: import nixpkgs { inherit system; };

      # Single source of truth for the version lives in CMakeLists.txt
      # (`project(hyprfm VERSION x.y.z ...)`) so it never has to be bumped
      # in two places.
      version = builtins.head (
        builtins.match ".*project\\(hyprfm VERSION ([0-9]+\\.[0-9]+\\.[0-9]+).*" (
          builtins.readFile ./CMakeLists.txt
        )
      );

      # Only the client-side GIO module is usable from an application closure:
      # gvfsd and its backends are D-Bus-activated per-session services, so a
      # bundled copy would never be activated. Referencing ${pkgs.gvfs}
      # directly to get one 292KB shim drags samba, udisks and gtk3 along --
      # 449MB of daemons that can never run from here. Copy the two libraries
      # out and repoint their RPATH instead; what is left references only glib
      # and libc, which the closure already has.
      gvfsClientModule =
        pkgs:
        pkgs.runCommand "gvfs-client-gio-module"
          { nativeBuildInputs = [ pkgs.patchelf pkgs.removeReferencesTo ]; }
          ''
            mkdir -p $out/lib/gio/modules
            cp ${pkgs.gvfs}/lib/gvfs/libgvfscommon.so $out/lib/
            cp ${pkgs.gvfs}/lib/gio/modules/libgvfsdbus.so $out/lib/gio/modules/
            chmod +w $out/lib/libgvfscommon.so $out/lib/gio/modules/libgvfsdbus.so
            patchelf --set-rpath "$out/lib:${pkgs.glib.out}/lib:${pkgs.stdenv.cc.libc}/lib" \
              $out/lib/gio/modules/libgvfsdbus.so
            patchelf --set-rpath "${pkgs.glib.out}/lib:${pkgs.stdenv.cc.libc}/lib" \
              $out/lib/libgvfscommon.so

            # patchelf leaves the replaced RPATH string in the binary, and nix
            # scans raw bytes for store hashes -- so without this the copy
            # still "references" gvfs and drags the whole 449MB back in.
            remove-references-to -t ${pkgs.gvfs} \
              $out/lib/libgvfscommon.so $out/lib/gio/modules/libgvfsdbus.so
          '';

      mkHyprfm =
        pkgs:
        pkgs.stdenv.mkDerivation {
          pname = "hyprfm";
          inherit version;

          src = self;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            kdePackages.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            kdePackages.qtbase
            kdePackages.qtdeclarative
            kdePackages.qtsvg
            kdePackages.qtwayland
            kdePackages.kwindowsystem
            glib
          ];

          # Submodules aren't fetched for a plain flake source; drop the
          # pinned inputs in where CMake/QML expect them instead.
          postPatch = ''
            rm -rf src/qml/icons src/qml/Quill
            cp -r --no-preserve=mode,ownership ${quill-icons} src/qml/icons
            cp -r --no-preserve=mode,ownership ${quill} src/qml/Quill
          '';

          cmakeFlags = [
            "-DBUILD_TESTS=OFF"
            "-DHYPRFM_DATA_DIR=${placeholder "out"}/share/hyprfm"
          ];

          qtWrapperArgs = [
            "--prefix"
            "PATH"
            ":"
            (pkgs.lib.makeBinPath [
              pkgs.rsync
              pkgs.glib # gio
              pkgs.xdg-utils
              pkgs.wl-clipboard

              # Compress/extract shells out to these by name. Without them
              # the archive menu entries are present but every one of them
              # fails, so they are not optional the way the tools below are.
              pkgs.gnutar
              pkgs.gzip
              pkgs.bzip2
              pkgs.xz
              pkgs.zstd
              pkgs.zip
              pkgs.unzip
              pkgs.p7zip # 7z
              pkgs.libarchive # bsdtar, the fallback for rar and odd formats

              # Optional, and each degrades gracefully on its own (search
              # falls back to a directory walk, previews to plain text or
              # highlighted source) while the startup dependency check names
              # whatever is missing. These four are cheap and cover features a
              # user would notice losing.
              # Deliberately not here: git (+385MB) and exiftool (+127MB) --
              # this is a --prefix, so a user who has them keeps using theirs,
              # and the git-status column and metadata panel simply stay empty
              # for anyone who does not. ffmpeg is out for the same reason.
              pkgs.fd
              pkgs.bat
              pkgs.md4c # md2html, for rendered Markdown previews
              pkgs.poppler-utils # pdftoppm, pdfinfo
            ])

            # File and folder icons come from the system icon theme via
            # QIcon::setThemeName() (default "Adwaita", falling back through
            # breeze/Papirus/Adwaita/hicolor). A desktop session normally
            # provides these through XDG_DATA_DIRS, but `nix run` on a minimal
            # or non-NixOS host has neither the variable nor the themes, and
            # every file and folder renders as blank space.
            "--prefix"
            "XDG_DATA_DIRS"
            ":"
            "${pkgs.adwaita-icon-theme}/share:${pkgs.hicolor-icon-theme}/share"

            # gvfs ships the client-side GIO module (libgvfsdbus.so). Without
            # it GIO cannot speak the gvfs protocol at all, so sftp:// smb://
            # and mtp:// transfers fail even when gvfsd is running — hyprfm
            # copies to those URIs in-process via g_file_copy(). NixOS with
            # services.gvfs.enable already exports this, but a plain `nix run`
            # on a non-NixOS host cannot borrow the host module: it is built
            # against the host's glib, not this closure's.
            "--prefix"
            "GIO_EXTRA_MODULES"
            ":"
            "${gvfsClientModule pkgs}/lib/gio/modules:${pkgs.glib-networking}/lib/gio/modules"
          ];

          meta = with pkgs.lib; {
            description = "A lightweight Qt6/QML file manager for Hyprland";
            homepage = "https://github.com/soyeb-jim285/hyprfm";
            license = licenses.mit;
            mainProgram = "hyprfm";
            platforms = systems;
          };
        };
    in
    {
      packages = forEachSystem (system: {
        default = self.packages.${system}.hyprfm;
        hyprfm = mkHyprfm (pkgsFor system);
      });

      apps = forEachSystem (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.hyprfm}/bin/hyprfm";
          meta.description = "A lightweight Qt6/QML file manager for Hyprland";
        };
      });

      devShells = forEachSystem (
        system:
        let
          pkgs = pkgsFor system;
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.hyprfm ];
          };
        }
      );
    };
}
