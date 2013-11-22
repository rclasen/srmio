require 'formula'

class Srmio < Formula
  homepage 'http://www.zuto.de/project/srmio/'
  url 'http://www.zuto.de/project/files/srmio/srmio-0.1.1~git1.tar.gz'
  sha1 '0db685d6046fca38ad64df05840d01b5f3b27499'

  head do
    url 'git://github.com/rclasen/srmio.git'
  end

  def install
    if build.head?
      system "chmod u+x genautomake.sh"
      system "./genautomake.sh"
    end
    system "./configure", "--disable-dependency-tracking",
                          "--prefix=#{prefix}"
    system "make install"
  end

  def test
    system "#{bin}/srmcmd", "--help"
  end
end
