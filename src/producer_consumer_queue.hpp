
/*
The following code have been copied and modified from BayesTyper (https://github.com/bioinformatics-centre/BayesTyper)
*/

/*
ProducerConsumerQueue.hpp - This file is part of BayesTyper (https://github.com/bioinformatics-centre/BayesTyper)


The MIT License (MIT)

Copyright (c) 2016 Jonas Andreas Sibbesen and Lasse Maretty

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/


#ifndef RPVG_SRC_PRODUCERCONSUMERQUEUE_HPP
#define RPVG_SRC_PRODUCERCONSUMERQUEUE_HPP

#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

template <typename Data>
class ProducerConsumerQueue {

	public:

		ProducerConsumerQueue(uint32_t); // Argument - max buffer size
		void push(Data);
		void pushedLast();
		bool pop(Data *);

	private:

		uint32_t max_buffer_size;

		bool pushed_last;

		std::deque<Data> queue;

		std::mutex queue_mutex;

		std::condition_variable producer_cv;
		std::condition_variable consumer_cv;
};

#include "producer_consumer_queue.tpp"

#endif
