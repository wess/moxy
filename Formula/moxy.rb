class Moxy < Formula
  desc "Lightweight superset of C that transpiles to portable C11"
  homepage "https://github.com/wess/moxy"
  url "https://github.com/wess/moxy/archive/refs/tags/v0.1.4.tar.gz"
  sha256 "05db70951fb535bff074bdf5c6fb7020effd87f527211b2a4ef629d2cf94c371"
  license "MIT"

  head "https://github.com/wess/moxy.git", branch: "main"

  def install
    system "git", "submodule", "update", "--init", "--recursive" if build.head?

    goose_src = %w[build config fs lock pkg cmake].map { |f| "libs/goose/src/#{f}.c" }
    goose_cmd = Dir["libs/goose/src/cmd/*.c"]
    yaml_src = Dir["libs/goose/libs/libyaml/src/*.c"]

    system ENV.cc, "-Wall", "-Wextra", "-std=c11", "-O2",
           "-Isrc", "-Ilibs/goose/src", "-Ilibs/goose/libs/libyaml/include",
           "-DYAML_VERSION_MAJOR=0", "-DYAML_VERSION_MINOR=2",
           "-DYAML_VERSION_PATCH=5", '-DYAML_VERSION_STRING="0.2.5"',
           *Dir["src/*.c"], *goose_src, *goose_cmd, *yaml_src,
           "-o", "moxy"
    bin.install "moxy"
  end

  test do
    (testpath/"hello.mxy").write("void main() { print(42); }\n")
    output = shell_output("#{bin}/moxy #{testpath}/hello.mxy")
    assert_match "printf", output
  end
end
