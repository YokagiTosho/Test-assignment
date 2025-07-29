#include <iostream>
#include <fstream>
#include <stdexcept>

#include "bmp_header.hpp"

using namespace bmp_types;

struct Pixel {
	BYTE r;
	BYTE g;
	BYTE b;
	BYTE a;
};

struct BMPHeaders {
	char *hdrs;
	DWORD size;

	BMPHeaders(char *hdrs, DWORD size) 
		: hdrs(hdrs), size(size)
	{
	}

	BMPHeaders(BMPHeaders &&h)
		: hdrs(h.hdrs), size(h.size)
	{
		h.hdrs = nullptr;
		h.size = 0;
	}

	~BMPHeaders() {
		delete []hdrs;
		hdrs = nullptr;
	}
};

struct BMPImageInfo {
	LONG width;
	LONG height;
	WORD bit_count;

	BMPImageInfo(LONG width, LONG height, WORD bit_count)
		: width(width), height(height), bit_count(bit_count)
	{}
};

class BMPImage {
private:
	Pixel **m_array_pixels;
	BMPImageInfo m_image_info;
	BMPHeaders m_hdrs;
public:
	BMPImage(Pixel **array_pixels, BMPImageInfo ii, BMPHeaders &&hdrs)
		: m_array_pixels(array_pixels), m_image_info(ii), m_hdrs(std::move(hdrs))
	{}

	~BMPImage() {
		for (LONG i = 0; i < m_image_info.height; i++) {
			delete []m_array_pixels[i];
		}
		delete []m_array_pixels;

		m_array_pixels = nullptr;
	}

	const BMPHeaders &hdrs() const {
		return m_hdrs;
	}

	const BMPImageInfo &image_info() const {
		return m_image_info;
	}

	Pixel& pixel_at(size_t x, size_t y) {
		return m_array_pixels[x][y];
	}

	const Pixel& pixel_at(size_t x, size_t y) const {
		return m_array_pixels[x][y];
	}

	LONG width() const {
		return m_image_info.width;
	}

	LONG height() const {
		return m_image_info.height;
	}

	void print_image() {
		for (LONG i = 0; i < m_image_info.height; i++) {
			for (LONG j = 0; j < m_image_info.width; j++) {
				auto &p = m_array_pixels[i][j];
				if (p.r == 0 && p.g == 0 && p.b == 0){ 
					std::cout << "@";
				} else if (p.r == 255 && p.g == 255 && p.b == 255) {
					std::cout << "*";
				}
			}
			std::cout << std::endl;
		}
	}
};


class BMPImageReader {
	// описание BMP формата было взято из следующих источников:
	// https://ru.wikipedia.org/wiki/BMP
	// https://learn.microsoft.com/ru-ru/windows/win32/api/wingdi/ns-wingdi-bitmapv5header
	// https://upload.wikimedia.org/wikipedia/commons/7/75/BMPfileFormat.svg
private:
	std::string m_path;
	std::ifstream m_file;

	DWORD m_off_bits; // положение пиксельных данных относительно начала файла (в байтах).
	LONG m_width;
	LONG m_height;
	WORD m_bit_count; // количество бит на пиксель
	

	template<typename T>
	T read_bytes() {
		T buf;
		m_file.read(reinterpret_cast<char*>(&buf), sizeof(buf));
		return buf;
	}

	bmp_types::WORD read_signature() {
		return read_bytes<WORD>();
	}

	template<int N>
	void skip_bytes() {
		m_file.seekg(N, std::ios::cur);
	}

	Pixel **read_array_pixels(bool top_down) {
		// jump to pixels array location
		m_file.seekg(m_off_bits, std::ios::beg);

		Pixel** array_pixels = new Pixel*[m_height];

		for (LONG i = 0; i < m_height; i++) {
			array_pixels[i] = new Pixel[m_width];
		}

		int padding = (4 - (m_width * 3 % 4)) % 4;

		for (LONG i = 0; i < m_height; i++) {
			LONG fixed_row = top_down ? (m_height - 1 - i) : i;
			for (LONG j = 0; j < m_width; j++) {
				Pixel pixel;

				pixel.b = read_bytes<BYTE>();
				pixel.g = read_bytes<BYTE>();
				pixel.r = read_bytes<BYTE>();
				
				if (m_bit_count == 32) {
					pixel.a = read_bytes<BYTE>();
				}

				array_pixels[fixed_row][j] = pixel;
			}

			if (padding > 0) {
				m_file.seekg(padding, std::ios::cur);
			}
		}

		return array_pixels;
	}

	BMPHeaders read_headers() {
		char *hdrs = new char[m_off_bits];;
		m_file.seekg(0, std::ios::beg);
		m_file.read(hdrs, m_off_bits);
		return { hdrs, m_off_bits };
	}
public:
	BMPImageReader(const std::string &path)
		: m_path(path)
	{
		m_file = std::ifstream(m_path, std::ios::binary);
		if (!m_file.is_open()) {
			throw std::runtime_error("Could not open file: \"" + path + "\"");
		}
	}

	BMPImage read_image() {
		// 0x4D42 in little endian
		if (read_signature() != 0x4D42) {
			throw std::runtime_error("Wrong file signature");
		}

		bool top_down = false;

		skip_bytes<8>();

		m_off_bits = read_bytes<DWORD>();

		skip_bytes<4>();

		m_width = read_bytes<LONG>();
		m_height = read_bytes<LONG>();

		if (m_height < 0) {
			top_down = true;
			m_height = std::abs(m_height);
		}

		skip_bytes<2>();

		m_bit_count = read_bytes<WORD>();

		auto array_pixels = read_array_pixels(top_down);

		// вместо чтения всех структур из файла поэлементно,
		// будут сохранены здесь в сыром формате и переданы BMPImage
		auto hdrs = read_headers();
	
		return BMPImage
			(
			 array_pixels,
			 {m_width, m_height, m_bit_count},
			 std::move(hdrs)
			);
	}
};

class BMPImageWriter {
private:
	BMPImage &m_image;
public:
	BMPImageWriter(BMPImage &image)
		: m_image(image)
	{}

	void write_image(const std::string &path) {
		std::ofstream ofs(path, std::ios::binary);
		if (!ofs) {
			throw std::runtime_error("Failed to open file \"" + path + "\"");
		}

		ofs.write(m_image.hdrs().hdrs, m_image.hdrs().size);

		const int padding = (4 - (m_image.width() * 3) % 4) % 4;

		for (size_t i = 0; i < m_image.height(); i++) {
			for (size_t j = 0; j < m_image.width(); j++) {
				ofs.put(m_image.pixel_at(i, j).b);
				ofs.put(m_image.pixel_at(i, j).g);
				ofs.put(m_image.pixel_at(i, j).r);

				if (m_image.image_info().bit_count == 32) {
					ofs.put(m_image.pixel_at(i, j).a);
				}
			}

			// padding
			for (int p = 0; p < padding; p++) {
				ofs.put(0);
			}
		}
	}
};

class BMPImageEditor {
private:
	BMPImage &m_image;
public:
	BMPImageEditor(BMPImage &img)
		: m_image(img)
	{}

	void set_pixel(LONG x, LONG y, const Pixel& color) {
		if (x >= 0 && x < m_image.width() && y >= 0 && y < m_image.height()) {
			m_image.pixel_at(x, y) = color;
		}
	}

	void draw_line(LONG x0, LONG y0, LONG x1, LONG y1, const Pixel& clr) {
		LONG dx = std::abs(x1 - x0);
		LONG dy = -std::abs(y1 - y0);
		LONG sx = (x0 < x1) ? 1 : -1;
		LONG sy = (y0 < y1) ? 1 : -1;
		LONG err = dx + dy;

		while (true) {
			set_pixel(x0, y0, clr);

			if (x0 == x1 && y0 == y1) break;

			LONG e2 = 2 * err;
			if (e2 >= dy) {
				if (x0 == x1) break;
				err += dy;
				x0 += sx;
			}
			if (e2 <= dx) {
				if (y0 == y1) break;
				err += dx;
				y0 += sy;
			}
		}
	}

	void draw_diagonal_cross(LONG x1, LONG y1, LONG x2, LONG y2, const Pixel& color) {
		draw_line(x1, y1, x2, y2, color);
		draw_line(x1, y2, x2, y1, color);
	}
};

int main(int argc, char **argv) {
	std::cout << "Enter input BMP filename: ";
	
	std::string in_filename;
  std::getline(std::cin, in_filename);

	auto bmp_reader = BMPImageReader(in_filename);

	auto bmp_image = bmp_reader.read_image();

	BMPImageEditor image_editor(bmp_image);

	image_editor.draw_diagonal_cross(40, 60, 160, 120, {0,0,0});

	bmp_image.print_image();

	std::cout << "Enter output BMP filename: ";
	std::string out_filename;
	std::getline(std::cin, out_filename);

	BMPImageWriter writer(bmp_image);
	writer.write_image(out_filename);

	return 0;
}
