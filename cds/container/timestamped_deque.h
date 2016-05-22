/*
 * timestamped_deque.h
 *
 *  Created on: 17 февр. 2016 г.
 *      Author: lightwave
 */

#ifndef CDSLIB_CONTAINER_TIMESTAMPED_DEQUE_H_
#define CDSLIB_CONTAINER_TIMESTAMPED_DEQUE_H_

#include <cds/gc/hp.h>
#include <atomic>
#include <boost/thread.hpp>
#include <cds/container/details/base.h>
#include <cds/compiler/timestamp.h>
#include <string.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace cds { namespace container {

//
 	/** @ingroup cds_nonintrusive_helper
 	*/
 	namespace timestamped_deque {
 		/// timestamped_deque default type traits
 		struct traits
 		{
 			/// Node allocator
 			using node_allocator = CDS_DEFAULT_ALLOCATOR;

 			using buffernode_allocator = CDS_DEFAULT_ALLOCATOR;
			using gnode_allocator = CDS_DEFAULT_ALLOCATOR;
 			/// Item allocator
 			using allocator = CDS_DEFAULT_ALLOCATOR;
 			/// Item counting feature; by default, disabled. Use \p cds::atomicity::item_counter to enable item counting
 			using item_counter = atomicity::empty_item_counter;
 		};

         template <typename... Options> struct make_traits
 		{
             typedef typename cds::opt::make_options <
                 typename cds::opt::find_type_traits< traits, Options... >::type
                 ,Options...
             >::type type;
         };
 	} // namespace timestamped_deque
    using namespace cds::timestamp;

 	template <typename T, typename Traits = cds::container::timestamped_deque::traits>
	class Timestamped_deque {
 		class ThreadBuffer;

 		struct buffer_node;
 		struct node {
 			std::atomic<unsigned long> timestamp;
 			T* item;
 			node(): timestamp(0) {}
 		};




	public:
		struct Statistic {
			std::atomic<int> failedPopLeft;
			std::atomic<int> failedPopRight;
			std::atomic<int> successPopLeft;
			std::atomic<int> successPopRight;
			std::atomic<int> pushLeft;
			std::atomic<int> pushRight;
			std::atomic<int> emptyPopLeft;
			std::atomic<int> emptyPopRight;
			std::atomic<int> poppedAmount;
			std::atomic<int> freedAmount;
			std::atomic<int> pushedAmount;
			std::atomic<int> delayedToFree;
			std::atomic<int> delayedFromDelete;
			std::atomic<int> delayedFromInsert;
			std::atomic<int> notAllowedUnlinking;
			std::atomic<int> wrongDelayed;
			std::atomic<int> putConflict;

			Statistic() {
				failedPopLeft.store(0);
				failedPopRight.store(0);
				successPopLeft.store(0);
				successPopRight.store(0);
				pushLeft.store(0);
				pushRight.store(0);
				emptyPopLeft.store(0);
				emptyPopRight.store(0);
				poppedAmount.store(0);
				freedAmount.store(0);
				pushedAmount.store(0);
				delayedToFree.store(0);
				delayedFromDelete.store(0);
				delayedFromInsert.store(0);
				notAllowedUnlinking.store(0);
				wrongDelayed.store(0);
				putConflict.store(0);
			}
		};

		class Logger;
		class EndlessLoopException;
	 	typedef T value_type;
	 	typedef Traits traits;
	 	typedef typename cds::details::Allocator<T, typename traits::allocator> allocator;
	 	typedef typename cds::details::Allocator<node, typename traits::node_allocator> node_allocator;
	 	typedef typename traits::item_counter item_counter;
	 	typedef std::atomic<buffer_node*> bnode_ptr;
	 	typedef typename ThreadBuffer::buffer_node   bnode;
	 	typedef cds::gc::HP::Guard guard;
	 	typedef std::pair<guard*, guard*> finded_ptr;

	private:
		Statistic stats;
 		ThreadBuffer* localBuffers;
 		std::atomic<int> lastFree;
 		int maxThread;

 		item_counter itemCounter;

 		bnode*** lastLefts;
 		bnode*** lastRights;
 		bool* wasEmpty;
		Logger *logger;


 		boost::thread_specific_ptr<int> threadIndex;

 		int acquireIndex() {
 			int* temp = threadIndex.get();
 			if(temp == nullptr) {
 				int index = lastFree.load();
 				if(index >= maxThread)
 					return -1;
 				while(!lastFree.compare_exchange_strong(index, index + 1)) {
 					index = lastFree.load();
 				}
 				threadIndex.reset(new int(index));
 				return index;
 			}
 			return *temp;
 		}

 		bool doEmptyCheck() {

			return itemCounter == 0;
 		}
 		/*
 		 *  Helping function for checking emptiness
 		 */
 		bool checkEmptyCondition(bool found , int i) {
 			int threadIND = acquireIndex();
			bnode *leftBorder = localBuffers[i].getLeftMost(),
				   *rightBorder = localBuffers[i].getRightMost();
			bool empty = leftBorder == rightBorder || (!found
					&& lastLefts[threadIND][i] == leftBorder
					&& lastRights[threadIND][i] == rightBorder);
			lastLefts[threadIND][i] = leftBorder;
			lastRights[threadIND][i] = rightBorder;
			return empty;
 		}

 		bool tryRemove(guard& toRemove, bool fromL, bool& success) {
			 guard candidate, startCandidate, startPoint;
			 candidate.clear();
			 startCandidate.clear();
			 startPoint.clear();
			 toRemove.clear();

			 bool empty = true, isFound = false;
			 success = true;
			 int threadIND = acquireIndex();
			 unsigned long startTime = platform::getTimestamp();
			 int bufferIndex = 0;
			 int start = rand() % maxThread,
			     i = start;
			 do {
//				 std::cout << "..";
//				 std::cout << (localBuffers[i].isSane(fromL) ? 's' : 'i') << "..";
				 if(localBuffers[i].get(candidate, startCandidate, fromL)) {
					 if(!isFound) {
						 isFound = true;
						 empty = false;
						 toRemove.copy(candidate);
						 startPoint.copy(startCandidate);
						 bufferIndex = i;
						 if(candidate.get<bnode>()->item->timestamp == 0)
							 break;
					 }
					 if(isMore(candidate.get<bnode>(), toRemove.get<bnode>(), fromL)) {
						 isFound = true;
						 empty = false;
						 toRemove.copy(candidate);
						 startPoint.copy(startCandidate);
						 bufferIndex = i;
						 if(candidate.get<bnode>()->item->timestamp == 0)
							 break;

					 }
				 }
				 i++;
				 if(i == maxThread)
					 i = 0;
			} while(i != start);
//			std::cout << ";\n";
			bool temp = wasEmpty[threadIND];
			wasEmpty[threadIND] = empty;
			empty = empty && temp;


			if(!isFound) {
//				std::cout << "Test:" << itemCounter << "\n";
				if(doEmptyCheck())
					return nullptr;
			}

			if(isFound) {
				bnode* borderNode = toRemove.get<bnode>();

				if(borderNode->wasAdded(fromL)) {
					if(localBuffers[bufferIndex].tryRemove(toRemove, startPoint, fromL)) {
						return true;
					}
				} else {
					if(borderNode->item->timestamp <= startTime)
						if(localBuffers[bufferIndex].tryRemove(toRemove, startPoint, fromL)) {
							return true;
						}
				}
			}
			success = false;
			return false;
		}

 		bool tryRemoveRight(guard &res, bool &success) {
			return tryRemove( res, false, success);
		}

 		bool tryRemoveLeft(guard &res, bool &success) {
			return tryRemove( res, true, success);
		}

 		bool isMore(bnode* n1, bnode* n2, bool fromL) {
			if(n1 == nullptr)
				return n2;
			else if(n2 == nullptr)
				return n1;

 			if(n1->item->timestamp == 0)
				return true;

			node *t1 = n1->item, *t2 = n2->item;
			if(n2->wasAddedLeft()) {
				if(n1->wasAddedRight())
					return !fromL;
				return fromL ? (t1->timestamp > t2->timestamp) : (t1->timestamp < t2->timestamp);
			} else {
				if(n1->wasAddedLeft())
					return fromL;
				return fromL ? (t1->timestamp < t2->timestamp) : (t1->timestamp > t2->timestamp);
			}

 		}

 		bool raw_push(value_type const& value, bool fromL) {
 			int index = acquireIndex();

			value_type* pvalue = allocator().New(value);
			node* timestamped = node_allocator().New();

			timestamped->item = pvalue;
			guard defender;
			itemCounter++;
			if(fromL)
				localBuffers[index].insertLeft(timestamped, defender);
			else
				localBuffers[index].insertRight(timestamped, defender);

			unsigned long t = platform::getTimestamp();
			timestamped->timestamp = t;
			defender.clear();
			if(fromL)
				stats.pushLeft++;
			else
				stats.pushRight++;
			stats.pushedAmount++;
			return true;
 		}

 		bool raw_push(value_type&& value, bool fromL) {
			int index = acquireIndex();

			value_type* pvalue = allocator().New(value);
			node* timestamped = node_allocator().New();

			timestamped->item = pvalue;
			guard defender;
			defender.clear();
			itemCounter++;
			if(fromL)
				localBuffers[index].insertLeft(timestamped, defender);
			else
				localBuffers[index].insertRight(timestamped, defender);

			unsigned long t = platform::getTimestamp();
			timestamped->timestamp = t;
			defender.clear();
			if(fromL)
				stats.pushLeft++;
			else
				stats.pushRight++;
			return true;
		}

 		bool raw_pop(value_type& val, bool fromL) {
 			guard res;
			bool success = false;
			do {
				tryRemove(res, fromL, success);
				if(!success) {
					if(fromL)
						stats.failedPopLeft++;
					else
						stats.failedPopRight++;
				}
			} while(!success);

			bnode* temp = res.get<bnode>();
			if(temp != nullptr) {
				if(fromL)
					stats.successPopLeft++;
				else
					stats.successPopRight++;
				stats.poppedAmount++;
				if(itemCounter == 0) {
					std::cout << "Danger";

				}
				itemCounter--;
				val = *temp->item->item;
				return true;
			} else {
				if(fromL)
					stats.emptyPopLeft++;
				else
					stats.emptyPopRight++;
				return false;
			}
 		}


		public:

		Timestamped_deque() {


			logger = new Logger();
			maxThread = cds::gc::HP::max_thread_count();
			localBuffers = new ThreadBuffer[maxThread];
			for(int i=0; i< maxThread; i++) {
				localBuffers[i].setStat(&stats);
				localBuffers[i].setLogger(logger);
				localBuffers[i].setIndex(i);
			}
			lastLefts = new bnode** [maxThread];
			// Initializing arrays for detecting empty state of container
			for(int i=0; i< maxThread; i++)
				lastLefts[i] = new bnode* [maxThread];
			lastRights = new bnode** [maxThread];
			for(int i=0; i< maxThread; i++)
				lastRights[i] = new bnode* [maxThread];
			wasEmpty = new bool [maxThread];
			for(int i=0; i< maxThread; i++)
				wasEmpty[i] = true;
			lastFree.store(0);

		}

		~Timestamped_deque() {
			delete [] localBuffers;
			for(int i=0; i<maxThread; i++) {
				delete [] lastLefts[i];
				delete [] lastRights[i];
			}
			delete [] wasEmpty;
			delete [] lastLefts;
			delete [] lastRights;
		}

		Logger* getLogger() {
			return logger;
		}



		bool push_back(value_type const& value) {
//			logger->write("push_back");
			return raw_push(value, true);
		}

		bool push_front(value_type const& value) {
//			logger->write("push_front");
			return raw_push(value, false);
		}

		bool push_back(value_type&& value ) {
//			logger->write("push_back");
			return raw_push(value, true);
		}

		bool push_front( value_type&& value	) {
//			logger->write("push_front");
			return raw_push(value, false);
		}

		bool pop_back(  value_type& val ) {
//			logger->write("pop_back");
			return raw_pop(val, true);
		}

		bool pop_front(  value_type& val ) {
//			logger->write("pop_front");
			return raw_pop(val, false);
		}

		bool empty() {
			doEmptyCheck();
			return doEmptyCheck();
		}

		void clear() {
			value_type v;
			while(pop_back(v)) {}
		}

		size_t size() const
		{
			return itemCounter.value();
		}

		int version() {

			return 14;
		}

		Statistic* getStats() {
			return &stats;
		}

		void printStats() {
			std::cout << "----------------------------------------------------------------------------------\n";
			std::cout << "VERS:1 \n";
			std::cout << "Amount of pushes         = " << stats.pushedAmount.load() << "\n";
			std::cout << "Amount of succesful pops = " << stats.poppedAmount.load() << "\n";
			std::cout << "Amount of empty pops     = " << stats.emptyPopLeft.load() + stats.emptyPopRight.load() << "\n";
			std::cout << "Amount of failed pops    = " << stats.failedPopLeft.load() + stats.failedPopRight.load() << "\n";
			std::cout << "Amount of freed nodes    = " << stats.freedAmount.load() << "\n";
			std::cout << "Amount of delayed        = " << stats.delayedToFree.load() << "\n";
			std::cout << "Amount of delayed insert = " << stats.delayedFromInsert.load() << "\n";
			std::cout << "Amount of delayed delete = " << stats.delayedFromDelete.load() << "\n";
			std::cout << "Amount of not allowed    = " << stats.notAllowedUnlinking.load() << "\n";
			std::cout << "Amount of wrong delayed  = " << stats.wrongDelayed.load() << "\n";
			std::cout << "Amount of put conflict   = " << stats.putConflict.load() << "\n";
			std::cout << "==================================================================================\n";
			std::cout << "Amount of succesful left pops  = " << stats.successPopLeft.load() << "\n";
			std::cout << "Amount of succesful right pops = " << stats.successPopRight.load() << "\n";
			std::cout << "Amount of empty left pops      = " << stats.emptyPopLeft.load() << "\n";
			std::cout << "Amount of empty right pops     = " << stats.emptyPopRight.load() << "\n";
			std::cout << "Amount of failed left pops     = " << stats.failedPopLeft.load() << "\n";
			std::cout << "Amount of failed right pops    = " << stats.failedPopRight.load() << "\n";
			std::cout << "Amount of left pushes          = " << stats.pushLeft.load() << "\n";
			std::cout << "Amount of right pushes         = " << stats.pushRight.load() << "\n";
			std::cout << "----------------------------------------------------------------------------------\n";

		}



		private:
		class ThreadBuffer {
		 		public:
		 			struct buffer_node {

		 				buffer_node() {
		 					taken.store(false);
		 					left.store(this);
		 					right.store(this);
							toInsert = false;
							delayed.clear();

		 				}

		 				std::atomic<buffer_node*> left;
		 				std::atomic<buffer_node*> right;
		 				node* item;
		 				int index;
		 				std::atomic<bool> taken;
						std::atomic<bool> toInsert;
						std::atomic_flag delayed;
		 				bool isDeletedFromLeft;
						buffer_node* go(bool toLeft) {
							return toLeft ? left.load() : right.load();
						}

						void setNeighbour(buffer_node* node, bool toLeft) {
							if(toLeft)
								left.store(node);
							else
								right.store(node);
						}

						bool trySetNeighbour(buffer_node* newOne, buffer_node* oldOne, bool toLeft) {
							if(toLeft)
								return left.compare_exchange_strong(oldOne, newOne);
							else
								return right.compare_exchange_strong(oldOne, newOne);
						}

		 				bool wasAdded(bool fromL) {
		 					if(fromL)
		 						return wasAddedLeft();
							else
								return wasAddedRight();
		 				}

		 				bool wasAddedRight() {
		 					return index > 0;
		 				}

		 				bool wasAddedLeft() {
		 					return index < 0;
		 				}

		 			};


		 		private:
					struct garbage_node {
						unsigned long timestamp;
						buffer_node* item;
						std::vector<buffer_node*> delayed;

						garbage_node() {
							timestamp = platform::getTimestamp();
						}

						garbage_node(buffer_node* item): item(item) {
							timestamp = platform::getTimestamp();
						}

						~garbage_node() {
							std::cout << "Gnode_delete" << std::endl;
						}
					};

		 			template <typename M>
					struct disposer {
						void operator ()( buffer_node * p ) {
							if(p->item != nullptr)
								allocator().Delete(p->item->item);
							node_allocator().Delete(p->item);
							buffernode_allocator().Delete(p);
						}
					};

					void cleanUnlinked(garbage_node* condemned, bool delayed) {
						for(typename std::vector<buffer_node*>::iterator it = condemned->delayed.begin(); it != condemned->delayed.end(); ++it) {
							freeNode(*it, delayed, condemned);
						}
					}




					void freeNode(disposer<buffer_node*> &executioner, buffer_node* toDel, bool delayed, garbage_node* origin = nullptr) {

						if(delayed)
							cds::gc::HP::retire<disposer<buffer_node> >(toDel);
						else
							executioner(toDel);
						std::stringstream ss;

						ss << '|' << index << "|Freed|" << toDel << "|" << origin;
						logger->write(ss.str());

						stats->freedAmount++;
					}

					void freeNode(buffer_node* toDel, bool delayed = true, garbage_node* origin = nullptr) {
						disposer<buffer_node*> executioner;
						freeNode(executioner, toDel, delayed, origin);
					}

					int findEmptyCell() {
						int place = -1;
						garbage_node* candidate;
						for(int i=0; i < garbageSize; i++ ) {
							candidate = garbageArray[i].load();
							if(candidate == nullptr) {
								place = i;
							}
						}
						return place;
					}

					void countGarbage(buffer_node* node) {

						buffer_node* cur = node;
						stats->delayedToFree++;
						if(!cur->taken.load())
							stats->wrongDelayed++;
//						std::stringstream ss1;
//						ss1 << '|' << index << "|Freed|" << cur;
//						logger->write(ss1.str());
						if(cur->isDeletedFromLeft) {
							while(cur->left.load() != cur) {
								cur = cur->left.load();
								stats->delayedToFree++;
								if(!cur->taken.load())
									stats->wrongDelayed++;
//								std::stringstream ss;
//								ss << '|' << index << "|Freed|" << cur;
//								logger->write(ss.str());

							}
						} else {
							while(cur->right.load() != cur) {
								cur = cur->right.load();
								stats->delayedToFree++;
								if(!cur->taken.load())
									stats->wrongDelayed++;
//								std::stringstream ss;
//								ss << '|' << index << "|Freed|" << cur;
//								logger->write(ss.str());

							}
						}

					}


					garbage_node* makeGarbageNode(buffer_node* node) {
						bool temp = false;

						garbage_node* gNode = gnode_allocator().New();
						gNode->item = node;

						buffer_node* cur = node,
								*next = node;
						bool fromLeft = cur->isDeletedFromLeft;
						do {
							temp = false;
							cur = next;
							if(!cur->delayed.test_and_set()) {
								std::stringstream ss1;
								ss1 << '|' << index << "|Delayed|" << cur;
								logger->write(ss1.str());
								stats->delayedToFree++;
								// delay
								gNode->delayed.push_back(cur);
								if(!cur->taken.load())
									stats->wrongDelayed++;

							}
							next = cur->go(fromLeft);

						} while(next != cur);
						gNode->timestamp = platform::getTimestamp();
						return gNode;
					}

					void putToGarbage(garbage_node* delayedChunk) {

						garbage_node* gNode = delayedChunk;
						garbage_node* tmp;

						int place = findEmptyCell();
						do {
							tmp = nullptr;
							place = -1;
							while(place == -1) {
								// Phew
								while (!tryToCleanGarbage(true)) { }
								place = findEmptyCell();
							}
						} while(!garbageArray[place].compare_exchange_strong(tmp, gNode));

					}

					bool tryToCleanGarbage(bool single = false) {
						unsigned long timestamp = platform::getTimestamp();
						bool cleaned = false;
						if(guestCounter.load() == 0) {
							int place = -1;
							garbage_node* candidate;
							for(int i=0; i < garbageSize; i++ ) {
								candidate = garbageArray[i].load();
								if(candidate != nullptr) {
									if (candidate->timestamp < timestamp) {
										place = i;


										if (garbageArray[i].compare_exchange_strong(candidate, nullptr)) {

											cleanUnlinked(candidate, true);
											cleaned = true;
											if (single)
												return cleaned;
										}
									}
								}
							}
							if(place == -1)
								return true;


						}
						return cleaned;
					}

					bool canBeUnlinked(buffer_node* node, bool fromL) {
						guestCounter++;
						buffer_node* cur = node;
						if(fromL) {
							do {
								if(cur->taken.load() && !cur->toInsert.load() && cur->left.load() != leftMost.load()) {
									cur = cur->left.load();
								} else {
									stats->notAllowedUnlinking++;
									guestCounter--;
									return false;
								}
							} while(cur->left.load() != cur);
						} else {
							do {
								if(cur->taken.load() && !cur->toInsert.load() && cur->right.load() != rightMost.load()) {
									cur = cur->right.load();
								} else {
									stats->notAllowedUnlinking++;
									guestCounter--;
									return false;
								}
							} while(cur->right.load() != cur);
						}
						guestCounter--;
						return  true;

					}

		 			std::atomic<buffer_node*> leftMost;
		 			std::atomic<buffer_node*> rightMost;
		 			std::vector<buffer_node*> garbage;
					std::atomic<garbage_node*> *garbageArray;
					int garbageSize;
					int index;
		 			long lastIndex;
					std::atomic<int> guestCounter;
					std::atomic<bool> inserting;
					Statistic* stats;
					Logger* logger;
		 		public:
					void setIndex(int index) {
						this->index = index;
					}
		 			typedef typename cds::details::Allocator<ThreadBuffer::buffer_node, typename traits::buffernode_allocator> buffernode_allocator;
					typedef typename cds::details::Allocator<ThreadBuffer::garbage_node, typename traits::gnode_allocator> gnode_allocator;

					ThreadBuffer() : lastIndex(1) {
						garbageSize = 20;
		 				buffer_node* newNode = buffernode_allocator().New();
		 				newNode->index = 0;
		 				newNode->item = nullptr;
		 				newNode->taken.store(true);
		 				leftMost.store(newNode);
		 				rightMost.store(newNode);
						inserting.store(false);
		 				guestCounter.store(0);
						garbageArray = new std::atomic<garbage_node*>[garbageSize];
						for(int i=0; i < garbageSize; i++)
							garbageArray[i].store(nullptr);

		 			}


		 			~ThreadBuffer() {
						buffer_node* curNode = rightMost.load();
						disposer<buffer_node*> executioner;
						while(curNode != curNode->left.load()) {
							buffer_node* temp = curNode;
							curNode = curNode->left.load();
							freeNode(executioner, temp, false);
						}
						freeNode(executioner, curNode, false);

					}

		 			buffer_node* getLeftMost() {
		 				return leftMost.load();
		 			}

		 			buffer_node* getRightMost() {
						return rightMost.load();
					}

					void setStat(Statistic* stats) {
						this->stats = stats;
					}

					void setLogger(Logger* log) {
						logger = log;
						std::stringstream ss;
						ss << '|' << index << "|Inserted|" << leftMost.load();
						logger->write(ss.str());
					}

					bool isBorder(buffer_node* node, bool leftest) {
						if(leftest)
							return node == leftMost.load();
						else
							return node == rightMost.load();
					}

					void setBorder(buffer_node* node, bool leftest) {
						if(leftest)
							leftMost.store(node);
						else
							rightMost.store(node);
					}

					bool tryToSetBorder(buffer_node* newOne, buffer_node* oldOne, bool leftest) {
						if(leftest)
							return leftMost.compare_exchange_strong(oldOne, newOne);
						else
							return rightMost.compare_exchange_strong(oldOne, newOne);
					}

					void insert(node* timestamped, guard& defender, bool toLeft) {

//						std::cout << "insert-start" << std::endl;
						buffer_node* newNode = buffernode_allocator().New();
						newNode->index = (toLeft ? -lastIndex : lastIndex);
						newNode->item = timestamped;
						lastIndex += 1;
						defender.protect(std::atomic<buffer_node*>(newNode));
						std::stringstream ss;
						ss << '|' << index << "|Inserted|" << newNode;
						guestCounter++;
						inserting.store(true);
						buffer_node* place = toLeft ? leftMost.load() : rightMost.load();
						buffer_node* next = place->go(!toLeft);

						while( next != place && place->taken.load()) {
							place = next;
							next = place->go(!toLeft);
						}

						buffer_node* tail = place->go(toLeft);

						if(place->go(!toLeft) == place)
							setBorder(place, !toLeft);

						newNode->setNeighbour(place, !toLeft);
						place->setNeighbour(newNode, toLeft);
						setBorder(newNode, toLeft);

						inserting.store(false);

						if(tail != place) {

							tail->isDeletedFromLeft = toLeft;
							garbage_node* garbage = makeGarbageNode(tail);
							guestCounter--;
							//putToGarbage(garbage);
							stats->delayedFromInsert++;
						} else {
							guestCounter--;
						}

//						std::cout << "insert-end" << std::endl;
					}

		 			void insertRight(node* timestamped, guard& defender) {
						insert(timestamped, defender, false);
		 			}

		 			void insertLeft(node* timestamped, guard& defender) {
						insert(timestamped, defender, true);
		 			}

					bool isSane(bool fromL) {
						buffer_node* res = fromL ? leftMost.load() : rightMost.load();
						buffer_node* lastOne = nullptr;
						std::map<buffer_node*, bool> visited;
						typename std::map<buffer_node*, bool>::iterator it;
						visited.insert(std::pair<buffer_node*, bool>(res, true));
						while(true) {
							if(isBorder(res,!fromL)) {
								guestCounter--;
								return true;
							}
							if(res->go(!fromL) == res) {
								guestCounter--;
								return false;
							}
							res = res->go(!fromL);
							it = visited.find(res);

							if(it == visited.end()) {
								visited.insert(std::pair<buffer_node*, bool>(res, true));
							} else {
								throw EndlessLoopException(visited.size());
							}
						}
					}

		 			bool get( guard& found ,guard& start, bool fromL) {

						std::stringstream ss;

						guestCounter++;
						start.protect( (fromL ? leftMost : rightMost));
						buffer_node *oldStart = start.get<buffer_node>(),
								*oldEnd = fromL ? rightMost.load() : leftMost.load();

						buffer_node* res = oldStart;
						buffer_node* lastOne = nullptr;
						std::map<buffer_node*, bool> visited;
						typename std::map<buffer_node*, bool>::iterator it;


						visited.insert(std::pair<buffer_node*, bool>(res, true));

						while(true) {
							if(!fromL && res->index < oldEnd->index || fromL && res->index > oldEnd->index) {
								guestCounter--;

								return false;
							}
							if(!res->taken.load()) {
								found.protect(std::atomic<buffer_node*>(res));

								break;
							}
							if(res->go(!fromL) == res) {
								guestCounter--;

								return false;
							}
							lastOne = res;
							res = res->go(!fromL);
							it = visited.find(res);

							if(it == visited.end()) {
								visited.insert(std::pair<buffer_node*, bool>(res, true));
							} else {

								logger->write(ss.str());
								throw EndlessLoopException(visited.size());
							}
						}
						guestCounter--;
						return true;
		 			}




		 			bool getRight(guard& found, guard& start) {
						return get(found, start, false);
		 			}

		 			bool getLeft(guard& found, guard& start) {
						return get(found, start, true);
					}


		 			bool tryRemove(guard& takenNode, guard& start, bool fromL) {
						guestCounter++;
						std::stringstream ss;
						buffer_node* node = takenNode.get<buffer_node>();
						buffer_node* startPoint = start.get<buffer_node>();
						buffer_node* temp = startPoint->go(fromL);
						buffer_node* oppositeBorder = fromL ? rightMost.load() : leftMost.load();
						// to delete, oldRight, to unlink
						bool t = false;
						if(node->taken.compare_exchange_strong(t, true)) {
							if( tryToSetBorder(node, startPoint, fromL)) {
								if( temp != node && !inserting.load() && isBorder(node, fromL) && isBorder(oppositeBorder, !fromL) && startPoint->trySetNeighbour(startPoint, temp, fromL)) {
									temp->isDeletedFromLeft = fromL;
									stats->delayedFromDelete++;
									garbage_node* garbage = makeGarbageNode(temp);
									guestCounter--;
									putToGarbage(garbage);

									return true;
								}
							}
							guestCounter--;
//							tryToCleanGarbage();

							//logger->write(ss.str());
							return true;
						}
						//logger->write(ss.str());
						guestCounter--;
						return false;
		 			}

		 			bool tryRemoveRight(guard& takenNode, guard& start) {
						return tryRemove(takenNode, start, false);
		 			}

		 			bool tryRemoveLeft(guard& takenNode, guard& start) {
						return tryRemove(takenNode, start, true);
		 			}



		 		}; // ThreadBuffer
	public:
				class Logger {
				private:
					struct LogNode {
						std::string note;
						unsigned long timestamp;
						int thread;
						LogNode(std::string note, unsigned long timestamp, int thread):note(note), timestamp(timestamp), thread(thread) {
						}
					};

					struct Comparator {
						bool operator() (LogNode* l1, LogNode* l2) {
							return l1->timestamp < l2->timestamp;
						}
					} comparator;

					std::vector<std::vector<LogNode*>* > threads;
					boost::thread_specific_ptr<int> threadIndex;
					std::atomic<int> lastFree;
					int maxThread;

					int acquireIndex() {
						int* temp = threadIndex.get();
						if(temp == nullptr) {
							int index = lastFree.load();
							if(index >= maxThread)
								return -1;
							while(!lastFree.compare_exchange_strong(index, index + 1)) {
								index = lastFree.load();
							}
							threadIndex.reset(new int(index));
							return index;
						}
						return *temp;
					}


				public:
					Logger() {
						maxThread = cds::gc::HP::max_thread_count();
						for(int i=0; i < maxThread; i++) {
							threads.push_back(new std::vector<LogNode*>());
						}
					}

					void write(std::string s) {
//						int index = acquireIndex();
//						LogNode* node = new LogNode(s, platform::getTimestamp(), index);
//						threads.at(index)->push_back(node);
					}

					void printAll() {
						std::vector<LogNode*> log;
						for(int i=0; i < maxThread; i++) {
							log.insert(log.end(), threads.at(i)->begin(), threads.at(i)->end());
						}
						std::sort(log.begin(), log.end(), comparator);
						for (typename std::vector<LogNode*>::iterator it = log.begin() ; it != log.end(); ++it)
							std::cout << (*it)->timestamp << "-" << (*it)->thread << ": " << (*it)->note << std::endl;

					}

					void printAll(std::ofstream& stream) {
						std::vector<LogNode*> log;
						for(int i=0; i < maxThread; i++) {
							log.insert(log.end(), threads.at(i)->begin(), threads.at(i)->end());
						}
						std::sort(log.begin(), log.end(), comparator);
						for (typename std::vector<LogNode*>::iterator it = log.begin() ; it != log.end(); ++it)
							stream << (*it)->timestamp << "-" << (*it)->thread << ": " << (*it)->note << std::endl;
					}





				}; // Logger

				class EndlessLoopException {
					unsigned long mapSize;

				public:
					EndlessLoopException(unsigned long size): mapSize(size) {

					}

					unsigned long getMapSize() {
						return mapSize;
					}
				};
	};// Timestamped_deque
}} // namespace cds::container

#endif /* CDSLIB_CONTAINER_TIMESTAMPED_DEQUE_H_ */
