{
  description = "ICO PC port: checks and a shell for the install pipeline (the game itself builds on Windows)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ];
      forAll = f: nixpkgs.lib.genAttrs systems (s: f nixpkgs.legacyPackages.${s});

      # Only what the pipeline imports at module level plus the recompiler's
      # own tool dependencies; versions come from nixpkgs, the Windows install
      # pins its own in pipeline/constraints.txt.
      pythonFor = pkgs: pkgs.python3.withPackages (ps: with ps; [
        capstone construct pycryptodome tabulate tqdm
      ]);

      # The repository without build output, so unrelated edits do not rerun the checks.
      src = nixpkgs.lib.cleanSourceWith {
        src = ./.;
        filter = path: type:
          let b = baseNameOf path; in !(builtins.elem b [ "build" "result" ".git" "bin" "obj" ]);
      };
    in {
      checks = forAll (pkgs:
        let python = pythonFor pkgs; in {
          # tests/test_smoke.py: profile, pipeline CLI and input checks, pins, release script
          smoke = pkgs.runCommand "ico-smoke" { nativeBuildInputs = [ python ]; } ''
            cp -r ${src} source && chmod -R u+w source && cd source
            python -I -m unittest discover -s tests -v
            touch $out
          '';
          lint = pkgs.runCommand "ico-lint" { nativeBuildInputs = [ pkgs.ruff ]; } ''
            cd ${src}
            ruff check --no-cache --isolated --select E9,F pipeline tools tests
            touch $out
          '';
        });

      devShells = forAll (pkgs: {
        default = pkgs.mkShell { packages = [ (pythonFor pkgs) pkgs.ruff pkgs.git ]; };
      });
    };
}
