
#ifndef __RADIX_TREE_H
#define __RADIX_TREE_H

#include <stdbool.h>
#include <stdint.h>

//基数树
//对于长整型数据的映射。
//怎样解决Hash冲突和Hash表大小的设计是一个非常头疼的问题。
//稀疏的长整型数据查找。借助于Radix树，我们能够实现对于长整型数据类型的路由。
//应用
//
//Radix树在Linux中的应用：

//Linux基数树（radix tree）是将long整数键值与指针相关联的机制，它存储有效率。而且可高速查询，用于整数值与指针的映射（如：IDR机制）、内存管理等。
//IDR（ID Radix）机制是将对象的身份鉴别号整数值ID与对象指针建立关联表。完毕从ID与指针之间的相互转换。
//IDR机制使用radix树状结构作为由id进行索引获取指针的稀疏数组，通过使用位图能够高速分配新的ID，IDR机制避免了使用固定尺寸的数组存放指针。IDR机制的API函数在lib/idr.c中实现。
//
//
//Linux radix树最广泛的用途是用于内存管理。结构address_space通过radix树跟踪绑定到地址映射上的核心页，该radix树同意内存管理代码高速查找标识为dirty或writeback的页。
//其使用的是数据类型unsigned long的固定长度输入的版本号。每级代表了输入空间固定位数。Linux radix树的API函数在lib/radix-tree.c中实现。（把页指针和描写叙述页状态的结构映射起来。使能高速查询一个页的信息。）
//
//Linux内核利用radix树在文件内偏移高速定位文件缓存页。
//Linux(2.6.7) 内核中的分叉为 64(2^6)。树高为 6(64位系统)或者 11(32位系统)，用来高速定位 32 位或者 64 位偏移，radix tree 中的每个叶子节点指向文件内相应偏移所相应的Cache项。
//
//【radix树为稀疏树提供了有效的存储，取代固定尺寸数组提供了键值到指针的高速查找。】


/*
 * This is a radix tree implementation mapping 64 bit keys to 64 bit values.
 * Its in-memory representation is also valid as a serial representation.
 */
struct radix_tree;
static const uint64_t radix_tree_invalid_value = (uint64_t) -1; 

/*
 * Lookup a key in the radix tree and return its value.  Returns 
 * radix_tree_invalid_value (ie -1) if not found 
 */
__attribute__((visibility("default")))
uint64_t 
radix_tree_lookup(struct radix_tree *tree, uint64_t key);

/*
 * Insert an range of keys into a radix tree (possibly reallocing it).  Returns true on
 * success.
 *
 * Arguments:
 *
 *   treep: The tree to modify.  Will write to *treep if the tree needs to be realloc'd
 *   key: The first key to set
 *   size: The number of keys to set
 *   value: The value to set them too

 */
bool 
radix_tree_insert(struct radix_tree **treep, uint64_t key, uint64_t size, uint64_t value);
/* 
 * Delete a range of keys from a radix tree.  Returns true on success.
 *
 * Arguments
 *
 *   treep: The tree to modify.  Will write to *treep if the tree needs to be realloc'd
 *   key: The first key to delete
 *   size: The number of keys to delete
 */
bool
radix_tree_delete(struct radix_tree **treep, uint64_t key, uint64_t size);
/*
 * Create a radix tree
 */
struct radix_tree *
radix_tree_create();
/*
 * deallocate a radix tree
 */
void
radix_tree_destory(struct radix_tree *tree);

/*
 * Count the number of keys in a radix tree.
 */
__attribute__((visibility("default")))
uint64_t
radix_tree_count(struct radix_tree *tree);
/*
 * Get the size of the radix tree buffer
 */
uint64_t
radix_tree_size(struct radix_tree *tree);

#endif
