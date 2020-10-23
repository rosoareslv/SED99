#pragma once

#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <experimental/optional>

#include <DB/Common/ProfileEvents.h>
#include <DB/Common/Stopwatch.h>

#include <DB/Common/Exception.h>
#include <DB/Common/CurrentMetrics.h>

#include <DB/IO/ReadBufferFromFileBase.h>
#include <DB/IO/ReadBuffer.h>
#include <DB/IO/WriteHelpers.h>
#include <DB/IO/BufferWithOwnMemory.h>


namespace DB
{

namespace ErrorCodes
{
	extern const int CANNOT_READ_FROM_FILE_DESCRIPTOR;
	extern const int ARGUMENT_OUT_OF_BOUND;
	extern const int CANNOT_SEEK_THROUGH_FILE;
	extern const int CANNOT_SELECT;
}

/** Работает с готовым файловым дескриптором. Не открывает и не закрывает файл.
  */
class ReadBufferFromFileDescriptor : public ReadBufferFromFileBase
{
protected:
	int fd;
	off_t pos_in_file; /// Какому сдвигу в файле соответствует working_buffer.end().

	bool nextImpl() override
	{
		size_t bytes_read = 0;
		while (!bytes_read)
		{
			ProfileEvents::increment(ProfileEvents::ReadBufferFromFileDescriptorRead);

			std::experimental::optional<Stopwatch> watch;
			if (profile_callback)
				watch.emplace(clock_type);

			ssize_t res = 0;
			{
				CurrentMetrics::Increment metric_increment{CurrentMetrics::Read};
				res = ::read(fd, internal_buffer.begin(), internal_buffer.size());
			}
			if (!res)
				break;

			if (-1 == res && errno != EINTR)
				throwFromErrno("Cannot read from file " + getFileName(), ErrorCodes::CANNOT_READ_FROM_FILE_DESCRIPTOR);

			if (res > 0)
				bytes_read += res;

			if (profile_callback)
			{
				ProfileInfo info;
				info.bytes_requested = internal_buffer.size();
				info.bytes_read = res;
				info.nanoseconds = watch->elapsed();
				profile_callback(info);
			}
		}

		pos_in_file += bytes_read;

		if (bytes_read)
		{
			ProfileEvents::increment(ProfileEvents::ReadBufferFromFileDescriptorReadBytes, bytes_read);
			working_buffer.resize(bytes_read);
		}
		else
			return false;

		return true;
	}

	/// Имя или описание файла
	std::string getFileName() const override
	{
		return "(fd = " + toString(fd) + ")";
	}

public:
	ReadBufferFromFileDescriptor(int fd_, size_t buf_size = DBMS_DEFAULT_BUFFER_SIZE, char * existing_memory = nullptr, size_t alignment = 0)
		: ReadBufferFromFileBase(buf_size, existing_memory, alignment), fd(fd_), pos_in_file(0) {}

	int getFD() const override
	{
		return fd;
	}

	off_t getPositionInFile() override
	{
		return pos_in_file - (working_buffer.end() - pos);
	}


private:

	/// Если offset такой маленький, что мы не выйдем за пределы буфера, настоящий seek по файлу не делается.
	off_t doSeek(off_t offset, int whence) override
	{
		off_t new_pos = offset;
		if (whence == SEEK_CUR)
			new_pos = pos_in_file - (working_buffer.end() - pos) + offset;
		else if (whence != SEEK_SET)
			throw Exception("ReadBufferFromFileDescriptor::seek expects SEEK_SET or SEEK_CUR as whence", ErrorCodes::ARGUMENT_OUT_OF_BOUND);

		/// Никуда не сдвинулись.
		if (new_pos + (working_buffer.end() - pos) == pos_in_file)
			return new_pos;

		if (hasPendingData() && new_pos <= pos_in_file && new_pos >= pos_in_file - static_cast<off_t>(working_buffer.size()))
		{
			/// Остались в пределах буфера.
			pos = working_buffer.begin() + (new_pos - (pos_in_file - working_buffer.size()));
			return new_pos;
		}
		else
		{
			ProfileEvents::increment(ProfileEvents::Seek);

			pos = working_buffer.end();
			off_t res = lseek(fd, new_pos, SEEK_SET);
			if (-1 == res)
				throwFromErrno("Cannot seek through file " + getFileName(), ErrorCodes::CANNOT_SEEK_THROUGH_FILE);
			pos_in_file = new_pos;
			return res;
		}
	}


	/// При условии, что файловый дескриптор позволяет использовать select, проверяет в течение таймаута, есть ли данные для чтения.
	bool poll(size_t timeout_microseconds)
	{
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		timeval timeout = { time_t(timeout_microseconds / 1000000), time_t(timeout_microseconds % 1000000) };

		int res = select(1, &fds, 0, 0, &timeout);

		if (-1 == res)
			throwFromErrno("Cannot select", ErrorCodes::CANNOT_SELECT);

		return res > 0;
	}
};

}
