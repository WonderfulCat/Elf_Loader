#include <stdio.h>
#include <dlfcn.h>
#include <elf.h>
#include <unistd.h>
#include <sys/mman.h>

#define PAGE_SIZE 0x1000
#define PAGE_MASK 0xFFF
#define IMAGE_BASE 0x80000000
#define ELF32_R_SYM(x) ((x) >> 8)
#define ELF32_R_TYPE(x) ((x) & 0xff)
static unsigned char __header[PAGE_SIZE];

int strcmp(const char *s1, const char *s2)
{
	while (*s1 == *s2++)
		if (*s1++ == 0)
			return (0);
	return (*(unsigned char *)s1 - *(unsigned char *)--s2);
}

static unsigned elfhash(const char *_name)
{
	const unsigned char *name = (const unsigned char *)_name;
	unsigned h = 0, g;

	while (*name)
	{
		h = (h << 4) + *name++;
		g = h & 0xf0000000;
		h ^= g;
		h ^= g >> 24;
	}
	return h;
}

void *memset(void *dst, int c, size_t n)
{
	char *q = dst;
	char *end = q + n;

	for (;;)
	{
		if (q < end)
			break;
		*q++ = (char)c;
		if (q < end)
			break;
		*q++ = (char)c;
		if (q < end)
			break;
		*q++ = (char)c;
		if (q < end)
			break;
		*q++ = (char)c;
	}

	return dst;
}

int main()
{
	//---------------------------- 打开文件 ----------------------------//
	FILE *fp = fopen("/data/user/ls", "rb");
	printf("fp - %p\n", fp);

	//---------------------------- 读取文件头 ----------------------------//
	/*
		ptr: 指向一个内存区域的指针，读取的数据将存储到该内存区域中。
		size: 每个元素的大小，以字节为单位。
		nmemb: 要读取的元素数量。
		stream: 指向文件对象的指针。
	*/
	fread(__header, sizeof(Elf32_Ehdr), 1, fp);
	Elf32_Ehdr *elf_hd = (Elf32_Ehdr *)__header;
	printf("head - %s\n", elf_hd->e_ident);

	//------------------------------ 读取&加载程序头 ---------------------------//
	/*
		stream: 指向文件对象的指针。
		offset: 文件位置指针的新位置，相对于 whence 的偏移量。
		whence: 决定文件位置指针如何移动的起始点。它可以取以下三个值之一：
				SEEK_SET: 文件开头
				SEEK_CUR: 当前文件位置指针
				SEEK_END: 文件末尾
	*/
	// 恢复文件指针
	fseek(fp, 0, SEEK_SET);
	// 读取elf头+程序头.  elf_hd->e_phoff == sizeof(Elf32_Ehdr) == 0x34
	fread(__header, elf_hd->e_phoff + sizeof(Elf32_Phdr) * elf_hd->e_phnum, 1, fp);
	// 程序头数组起始地址
	Elf32_Phdr *phdr = (Elf32_Phdr *)(elf_hd->e_phoff + __header);

	// 遍历程序头. phdr++(指针+1) = 数组索引+1
	for (int i = 0; i < elf_hd->e_phnum; ++i, ++phdr)
	{
		// 加载PT_LOAD程序头
		if (phdr->p_type == PT_LOAD)
		{
			// 起始地址4K对齐
			uint32_t start_addr = IMAGE_BASE + (phdr->p_vaddr & (~PAGE_MASK));
			/*
				如: 本来数据是从1855c读取p_filesz个字节.
					但由于内存对齐,导致从18000开始读取数据,如果还按p_filesz读取字节,数据则读不全.
					这里需要 p_filesz + 1855c & 0xFFF 才可以读完数据.
				+---------------+
				|	80018000	|	4K对齐起始地址
				|	8001855c	|	数据起始地址
				|	........	|
				+---------------+
			*/
			unsigned int len = phdr->p_filesz + (phdr->p_vaddr & PAGE_MASK);

			// 加载段到内存
			unsigned char *pbase = mmap((void *)start_addr, len, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fileno(fp), phdr->p_offset & (~PAGE_MASK));
			printf("mmap = %p\n", pbase);

			// 如果len不是刚好对齐到4K边界,多余部分用0填充
			if ((len & PAGE_MASK) && (phdr->p_flags & PF_W))
				memset((void *)(pbase + len), 0, PAGE_SIZE - (len & PAGE_MASK));

			/* Check to see if we need to extend the map for this segment to
			 * cover the diff between filesz and memsz (i.e. for bss).
			 *
			 *  base           _+---------------------+  page boundary
			 *                  .                     .
			 *                  |                     |
			 *                  .                     .
			 *  pbase          _+---------------------+  page boundary
			 *                  |                     |
			 *                  .                     .
			 *  base + p_vaddr _|                     |
			 *                  . \          \        .
			 *                  . | filesz   |        .
			 *  pbase + len    _| /          |        |
			 *     <0 pad>      .            .        .
			 *  extra_base     _+------------|--------+  page boundary
			 *               /  .            .        .
			 *               |  .            .        .
			 *               |  +------------|--------+  page boundary
			 *  extra_len->  |  |            |        |
			 *               |  .            | memsz  .
			 *               |  .            |        .
			 *               \ _|            /        |
			 *                  .                     .
			 *                  |                     |
			 *                 _+---------------------+  page boundary
			 */
			// 判断p_filesz是否小于p_memsz, 如果小于则需要再分配空间
			/*
				tmp = 对齐后分配的空间大小.
				1. 如果原空间己经是对齐的 (PAGE_SIZE - 1) & (~PAGE_MASK) 刚好是对齐空间. 如: 1000 + FFF & FFFFF000 = 1000
				2. 如果使用(PAGE_SIZE) & (~PAGE_MASK)就会出现错误. 如: 1000 + 1000 & FFFFF000 = 2000
				3. IMAGE_BASE + phdr->p_vaddr + phdr->p_memsz = 未对齐时内存所需空间
				4. 因为空间对齐的原因,p_memsz > p_filesz 时有可能不需要再分配额外空间. 对齐的空间可能己经包含了p_memsz > p_filesz的那部分额外空间.
				5. 所以使用 (未对齐时内存所需空间 - 对齐后分配的空间大小) 来判断是否还需分配额外空间
			*/

			uint32_t tmp = (start_addr + len + PAGE_SIZE - 1) & (~PAGE_MASK);
			if (tmp < IMAGE_BASE + phdr->p_vaddr + phdr->p_memsz)
			{
				unsigned int extra_len = IMAGE_BASE + phdr->p_vaddr + phdr->p_memsz - tmp;
				unsigned char *extra_base = mmap((void *)tmp, extra_len, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
				printf("mmap = %p\n", extra_base);
			}
		}
	}
	fclose(fp);
	printf("mmap ok\n");

	//---------------------------- 查找动态段----------------------------//
	// 内存程序头地址
	phdr = (Elf32_Phdr *)(IMAGE_BASE + elf_hd->e_phoff);
	// 动态段数据
	Elf32_Dyn *dyn_entry, *p;
	// 查找动态段地址
	for (int i = 0; i < elf_hd->e_phnum; ++i, ++phdr)
	{
		if (phdr->p_type == PT_DYNAMIC)
		{
			dyn_entry = (Elf64_Dyn *)(IMAGE_BASE + phdr->p_vaddr);
			printf("dyn_entry : %p\n", dyn_entry);
			break;
		}
	}
	//---------------------------- 解析动态段(字符串表)----------------------------//
	// 查找字符串表
	p = dyn_entry;
	char *str_tab;
	while (p->d_tag)
	{
		// 字符串表
		if (p->d_tag == DT_STRTAB)
		{
			str_tab = (char *)(IMAGE_BASE + p->d_un.d_ptr);
			printf("\t DT_STRTAB : %p\n", str_tab);
			break;
		}
		p++;
	}
	printf("\t --------------------\n");
	//---------------------------- 解析动态段(导入库表)----------------------------//
	// 导入库表
	p = dyn_entry;
	void *needed_lib[128];
	int needed_lib_len = 0;
	while (p->d_tag)
	{
		if (p->d_tag == DT_NEEDED)
		{
			printf("\t DT_NEEDED : %s\n", str_tab + p->d_un.d_ptr);
			needed_lib[needed_lib_len++] = dlopen(str_tab + p->d_un.d_ptr, RTLD_NOW);
		}

		p++;
	}
	printf("\t --------------------\n");
	//---------------------------- 解析动态段(符号表)----------------------------//
	// 符号表
	p = dyn_entry;
	Elf32_Sym *sym_table;
	while (p->d_tag)
	{
		if (p->d_tag == DT_SYMTAB)
		{
			sym_table = (Elf32_Sym *)(IMAGE_BASE + p->d_un.d_ptr);
			printf("\t DT_SYMTAB : %p\n", sym_table);
			break;
		}
		p++;
	}
	printf("\t --------------------\n");
	//---------------------------- 解析动态段(导入表)----------------------------//
	p = dyn_entry;
	Elf32_Rel *import_table;
	while (p->d_tag)
	{
		if (p->d_tag == DT_JMPREL)
		{
			import_table = (Elf32_Rel *)(IMAGE_BASE + p->d_un.d_ptr);
			printf("\t DT_JMPREL : %p\n", import_table);
			break;
		}
		p++;
	}
	//---------------------------- 解析动态段(导入表大小)----------------------------//
	p = dyn_entry;
	uint32_t import_table_count;
	while (p->d_tag)
	{
		if (p->d_tag == DT_PLTRELSZ)
		{
			import_table_count = p->d_un.d_val / sizeof(Elf32_Rel);
			printf("\t DT_PLTRELSZ : %d\n", import_table_count);
			break;
		}
		p++;
	}
	printf("\t --------------------\n");

	//---------------------------- 填充导入表数据 ----------------------------//
	for (int i = 0; i < import_table_count; ++i, ++import_table)
	{
		uint32_t type = ELF32_R_TYPE(import_table->r_info); // 类型
		uint32_t sym = ELF32_R_SYM(import_table->r_info);	// 符号表偏移
		uint32_t reloc = (uint32_t)(import_table->r_offset + IMAGE_BASE);
		// printf("\t DT_JMPREL-TYPE : %d\n", type);

		uint32_t addr; // 查找到的地址
		// 遍历所有导入库,查找函数地址写入导入表
		for (int i = 0; i < needed_lib_len; i++)
		{
			void *dl_sym = dlsym(needed_lib[i], str_tab + (sym_table + sym)->st_name);
			if (dl_sym)
			{
				addr = (uint32_t)dl_sym;
				break;
			}
		}
		// printf("addr : %x\n", addr);

		switch (type)
		{
		case R_ARM_JUMP_SLOT:
		case R_ARM_GLOB_DAT: // 这2项处理方式相同
			*((uint32_t *)reloc) = addr;
			break;
		case R_ARM_ABS32:
			*((uint32_t *)reloc) += addr;
			break;
		}
	}

	//---------------------------- 解析动态段(重定位表)----------------------------//
	p = dyn_entry;
	Elf32_Rel *rel_table;
	while (p->d_tag)
	{
		if (p->d_tag == DT_REL)
		{
			rel_table = (Elf32_Rel *)(IMAGE_BASE + p->d_un.d_ptr);
			printf("\t DT_REL : %p\n", rel_table);
			break;
		}
		p++;
	}
	//---------------------------- 解析动态段(重定位表大小)----------------------------//
	p = dyn_entry;
	uint32_t rel_table_count;
	while (p->d_tag)
	{
		if (p->d_tag == DT_RELSZ)
		{
			rel_table_count = p->d_un.d_val / sizeof(Elf32_Rel);
			printf("\t DT_RELSZ : %d\n", rel_table_count);
			break;
		}
		p++;
	}
	//---------------------------- 填充重定位表数据 ----------------------------//
	for (int i = 0; i < rel_table_count; ++i, ++rel_table)
	{
		uint32_t type = ELF32_R_TYPE(rel_table->r_info); // 类型
		uint32_t sym = ELF32_R_SYM(rel_table->r_info);	 // 符号表偏移
		uint32_t reloc = (uint32_t)(rel_table->r_offset + IMAGE_BASE);
		// printf("\t DT_REL-TYPE : %d\n", type);

		uint32_t addr = 0; // 查找到的地址
		// 遍历所有导入库,查找函数地址写入导入表
		for (int i = 0; i < needed_lib_len; i++)
		{
			void *dl_sym = dlsym(needed_lib[i], str_tab + (sym_table + sym)->st_name);
			if (dl_sym)
			{
				addr = (uint32_t)dl_sym;
				break;
			}
		}
		// printf("addr : %x\n", addr);

		switch (type)
		{
		case R_ARM_JUMP_SLOT:
		case R_ARM_GLOB_DAT: // 这2项处理方式相同
			*((uint32_t *)reloc) = addr;
			break;
		case R_ARM_ABS32:
			*((uint32_t *)reloc) += addr;
			break;
		case R_ARM_RELATIVE:
			*((uint32_t *)reloc) += IMAGE_BASE;
			break;
		}
	}
	printf("\t --------------------\n");

	//---------------------------- 导出表 ----------------------------//
	p = dyn_entry;
	uint32_t nbucket, nchain;
	uint32_t *bucket, *chain;

	while (p->d_tag)
	{
		if (p->d_tag == DT_HASH)
		{
			nbucket = ((unsigned *)(IMAGE_BASE + p->d_un.d_ptr))[0];
			nchain = ((unsigned *)(IMAGE_BASE + p->d_un.d_ptr))[1];
			bucket = (unsigned *)(IMAGE_BASE + p->d_un.d_ptr + 8);
			chain = (unsigned *)(IMAGE_BASE + p->d_un.d_ptr + 8 + nbucket * 4);

			printf("\t nbucket : %x\n", nbucket);
			printf("\t nchain : %x\n", nchain);

			printf("\t bucket : %x\n", bucket);
			printf("\t chain : %x\n", chain);
			break;
		}
		p++;
	}

	for (unsigned i = 0; i < nbucket; i++)
	{
		for (unsigned n = bucket[i]; n != 0; n = chain[n])
		{
			// printf("%s,%x,%d,%d, %x\n", str_tab + (sym_table + n)->st_name, elfhash(str_tab + (sym_table + n)->st_name), i, n, &chain[n]);

			switch (ELF32_ST_BIND((sym_table + n)->st_info))
			{
			case STB_GLOBAL:
				if ((sym_table + n)->st_shndx == 0)
					continue;
			case STB_WEAK:
				printf("\t %s, ELF32_ST_BIND = %d,st_shndx = %d, addr = %x\n", str_tab + (sym_table + n)->st_name, ELF32_ST_BIND((sym_table + n)->st_info), (sym_table + n)->st_shndx, (sym_table + n)->st_value);
				break;
			}
		}
	}
	printf("======================================\n");

	typedef int (*START)(int a, int b, int c, int d, int argc, char *argv, ...);
	START fun = (START)(elf_hd->e_entry + IMAGE_BASE);
	// argc = len(argv)
	// argv = {参数列表}
	fun(0, 0, 0, 0, 2, "./ls", "-la");
	return 0;
}