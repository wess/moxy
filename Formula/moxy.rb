class Moxy < Formula
  desc "Lightweight superset of C that transpiles to portable C11"
  homepage "https://github.com/wess/moxy"
  url "https://github.com/wess/moxy/archive/refs/tags/v0.1.1.tar.gz"
  sha256 "PLACEHOLDER"
  license "MIT"

  head "https://github.com/wess/moxy.git", branch: "main"

  def install
    system ENV.cc, "-Wall", "-Wextra", "-std=c11", "-O2", "-Isrc",
           *Dir["src/*.c"], "-o", "moxy"
    bin.install "moxy"
  end

  test do
    (testpath/"hello.mxy").write("void main() { print(42); }\n")
    output = shell_output("#{bin}/moxy #{testpath}/hello.mxy")
    assert_match "printf", output
  end
end
