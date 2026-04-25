class GSystemctl < Formula
  desc "Terminal UI for managing system services"
  homepage "https://github.com/shakg/g-systemctl"
  url "https://github.com/shakg/g-systemctl/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "REPLACE_WITH_RELEASE_TARBALL_SHA256"
  license "MIT"
  head "https://github.com/shakg/g-systemctl.git", branch: "main"

  depends_on "cmake" => :build

  def install
    system "cmake", "-S", ".", "-B", "build",
           "-DCMAKE_BUILD_TYPE=Release",
           "-DG_SYSTEMCTL_VERSION=#{version}",
           *std_cmake_args
    system "cmake", "--build", "build", "--parallel"
    system "cmake", "--install", "build"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/g-systemctl --version")
  end
end
