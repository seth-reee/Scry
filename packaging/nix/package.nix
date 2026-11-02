{ lib, stdenv, cmake, ninja, qt6, source, pkg-config, libzip, libglvnd, mesa, xvfb-run }:

stdenv.mkDerivation {
  pname = "scry";
  version = builtins.elemAt (builtins.match ".*project\\(Scry VERSION ([^ ]+) LANGUAGES.*"
    (builtins.readFile ../../CMakeLists.txt)) 0;
  src = lib.cleanSourceWith {
    src = source;
    filter = path: type:
      let name = baseNameOf path; in
      !(lib.hasInfix ".sync-conflict-" name)
      && !(builtins.elem name [ ".git" "build" "dist" "stage" "result" ])
      && !(lib.hasSuffix ".pkg.tar.zst" name)
      && !(lib.hasSuffix ".pkg.tar.xz" name);
  };
  nativeBuildInputs = [ cmake ninja qt6.wrapQtAppsHook pkg-config ];
  buildInputs = [ qt6.qtbase qt6.qtwayland libzip libglvnd ];
  nativeCheckInputs = [ xvfb-run ];
  doCheck = true;
  checkPhase = ''
    runHook preCheck
    export HOME="$TMPDIR/home"
    mkdir -p "$HOME"
    export QT_QPA_PLATFORM=xcb QT_QPA_PLATFORMTHEME=
    export LIBGL_ALWAYS_SOFTWARE=1
    export LIBGL_DRIVERS_PATH=${mesa}/lib/dri
    export LD_LIBRARY_PATH=${lib.makeLibraryPath [ mesa ]}
    xvfb-run -a ./scry --diagnostics ../packaging/ci-triangle.stl | tee render.log
    grep -Fx 'Model render: passed' render.log
    grep -Fx 'Triangles: 1' render.log
    ./scry --version | grep -Fx "scry $version"
    runHook postCheck
  '';


  meta = {
    description = "STL and 3MF model previewer";
    homepage = "https://github.com/seth-reee/Scry";
    license = lib.licenses.mit;
    mainProgram = "scry";
    platforms = [ "x86_64-linux" "aarch64-linux" ];
  };
}
