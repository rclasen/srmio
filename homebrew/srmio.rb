require 'formula'

class Srmio < Formula
  homepage 'http://www.zuto.de/project/srmio/'
  url 'http://www.zuto.de/project/files/srmio/srmio-0.1.1~git1.tar.gz'
  #sha1 'see below'
  # sha1 value is a chicken/egg problem: Formula is included in the tar.gz
  # and thereby affecting the hash, itself. Please updated as needed.

  def install

    system "./configure", "--disable-debug", "--disable-dependency-tracking",
                          "--prefix=#{prefix}"
    system "make install"
  end

  def test
    system "srmcmd --help"
  end
end
