#include "../aoi.h"
#include "systime.h"


#define BLOCK(M,X,Y) (&M->blocks[Y*M->x_size+X])

struct aoi_map *create_map(uint32_t max_aoi_objs,uint32_t _length,uint32_t radius,uint32_t max_radius,
					   struct point2D *top_left,struct point2D *bottom_right)
{
	uint32_t length = abs(top_left->x - bottom_right->x);
	uint32_t width = abs(top_left->y - bottom_right->y);
	
	if(radius > max_radius) return NULL;
	if(radius > length || radius > width) return NULL;
	if(max_radius > length || max_radius > width) return NULL;
	
	uint32_t x_size = length % _length == 0 ? length/_length : length/_length + 1;
	uint32_t y_size = width % _length == 0 ? width/_length : width/_length + 1;
	
	struct aoi_map *m = calloc(1,x_size*y_size*sizeof(struct aoi_block)+sizeof(struct aoi_map));
	m->top_left = *top_left;
	m->bottom_right = *bottom_right;
	m->x_size = x_size;
	m->y_size = y_size;
	m->radius = radius;
	m->max_radius = max_radius;
	m->max_aoi_objs = max_aoi_objs;
	uint32_t x,y;
	uint32_t index = 0;
	for(y = 0; y < y_size; ++y){
		for(x = 0; x < x_size; ++x)
		{
			struct aoi_block *b = BLOCK(m,x,y);
			dlist_init(&b->aoi_objs);
			b->x = x;
			b->y = y;
			b->index = index++; 
		}
	}
	
	//用最大视距计算一个视距最多可能覆盖多少个单元格
	uint32_t max_radius_blocksize = max_radius%_length == 0? max_radius/_length : max_radius/_length+1;
	max_radius_blocksize += 1;
	max_radius_blocksize *= max_radius_blocksize;
	m->new_block_set = new_bitset(max_radius_blocksize);
	m->old_block_set = new_bitset(max_radius_blocksize);
	m->new_blocks = calloc(1,sizeof(struct aoi_block*)*(max_radius_blocksize+1));
	m->old_blocks = calloc(1,sizeof(struct aoi_block*)*(max_radius_blocksize+1));
	m->enter_blocks = calloc(1,sizeof(struct aoi_block*)*(max_radius_blocksize+1));
	m->unchange_blocks = calloc(1,sizeof(struct aoi_block*)*(max_radius_blocksize+1));
	m->leave_blocks = calloc(1,sizeof(struct aoi_block*)*(max_radius_blocksize+1));
	m->_idmgr = new_idmgr(0,max_aoi_objs-1);
	return m;
}

static inline struct aoi_block *get_block_by_point(struct aoi_map *m,struct point2D *pos)
{	
	uint32_t x = pos->x - m->top_left.x;
	uint32_t y = pos->y - m->top_left.y;
	x = x/m->radius;
	y = y/m->radius;
	if(x > m->x_size || y > m->y_size)
		return NULL;
	return BLOCK(m,y,x);
}

static inline struct aoi_block **cal_blocks(struct aoi_map *m,struct aoi_block **blocks,
										   struct point2D *pos,uint32_t radius)
{
	if(get_block_by_point(m,pos) == NULL)
		return NULL;

	uint8_t c = 0;

	struct point2D top_left,bottom_right;
	top_left.x = pos->x - radius;
	top_left.y = pos->y - radius;
	bottom_right.x = pos->x + radius;
	bottom_right.y = pos->y + radius;
	
	if(top_left.x < m->top_left.x) top_left.x = m->top_left.x;
	if(top_left.y < m->top_left.y) top_left.y = m->top_left.y;
	if(bottom_right.x >= m->bottom_right.x) bottom_right.x = m->bottom_right.x-1;
	if(bottom_right.y >= m->bottom_right.y) bottom_right.y = m->bottom_right.y-1;
	
	struct aoi_block *_top_left = get_block_by_point(m,&top_left);
	struct aoi_block *_bottom_right = get_block_by_point(m,&bottom_right);
	bit_set_t block_set = blocks == m->new_blocks?m->new_block_set:m->old_block_set;
	uint32_t y = _top_left->y;
	for(; y <= _bottom_right->y; ++y){
		uint32_t x = _top_left->x;
		for(;x <= _bottom_right->x;++x){
			struct aoi_block *block = BLOCK(m,x,y);
			set_bit(block_set,block->index);
			blocks[c++] = block;	
		}			
	}
	blocks[c] = NULL;
	return blocks;
}

#define NEW_BLOCKS(M,POS,RADIUS) cal_blocks(M,M->new_blocks,POS,RADIUS)
#define OLD_BLOCKS(M,POS,RADIUS) cal_blocks(M,M->old_blocks,POS,RADIUS)

//计算新进入，离开，没变化的block集合
static int8_t cal_blockset(struct aoi_map *m,struct point2D *newpos,struct point2D *oldpos,uint32_t radius)
{
	struct aoi_block **new_blocks = NEW_BLOCKS(m,newpos,radius);		
	struct aoi_block **old_blocks = OLD_BLOCKS(m,oldpos,radius);
	if(new_blocks && old_blocks)
	{
		uint32_t i = 0;
		uint32_t c1 = 0;
		uint32_t c2 = 0;
		uint32_t c3 = 0;
		for(; old_blocks[i] ;++i){
			if(is_set(m->new_block_set,old_blocks[i]->index))
				m->unchange_blocks[c1++] = old_blocks[i];//新老集合中都存在
			else
				m->leave_blocks[c2++] = old_blocks[i];//新集合中不存在	
		}
		m->unchange_blocks[c1] = NULL;
		m->leave_blocks[c2] = NULL;
		
		for(i = 0; new_blocks[i] ;++i){
			if(is_set(m->old_block_set,new_blocks[i]->index) == 0)
				m->enter_blocks[c3++] = new_blocks[i];//老集合中不存在
		}
		m->enter_blocks[c3] = NULL;
		
		//清理new_block_set,old_block_set
		for(i = 0; new_blocks[i];++i)
			clear_bit(m->new_block_set,new_blocks[i]->index);
		for(i = 0; old_blocks[i];++i)
			clear_bit(m->old_block_set,old_blocks[i]->index);
		return 0;
	}
	return -1;
}

static inline void enter_me(struct aoi_object *me,struct aoi_object *other)
{
	if(me->aoi_object_id == other->aoi_object_id)
		printf("here\n");
	set_bit(me->view_objs,other->aoi_object_id);
	me->cb_enter(me,other);
}

static inline void leave_me(struct aoi_object *me,struct aoi_object *other)
{
	clear_bit(me->view_objs,other->aoi_object_id);
	me->cb_leave(me,other);
}

static inline void block_process_enter(struct aoi_map *m,struct aoi_block *bl,struct aoi_object *o)
{
	if(dlist_empty(&bl->aoi_objs))return;
	struct aoi_object *cur = (struct aoi_object*)dlist_first(&bl->aoi_objs);
	while(cur != (struct aoi_object*)DLIST_TAIL(&bl->aoi_objs))
	{
		if(!is_set(o->view_objs,cur->aoi_object_id) && o->in_myscope(o,cur))enter_me(o,cur);
		if(!is_set(cur->view_objs,o->aoi_object_id) && cur->in_myscope(cur,o))enter_me(cur,o);
		cur = (struct aoi_object *)cur->block_node.next;
	}
}

static inline void block_process_unchange(struct aoi_map *m,struct aoi_block *bl,struct aoi_object *o)
{
	if(dlist_empty(&bl->aoi_objs))return;
	struct aoi_object *cur = (struct aoi_object*)dlist_first(&bl->aoi_objs);
	while(cur != (struct aoi_object*)DLIST_TAIL(&bl->aoi_objs))
	{	
		if(o->in_myscope(o,cur)){
			 if(!is_set(o->view_objs,cur->aoi_object_id))
				enter_me(o,cur);
		}else{
			 if(is_set(o->view_objs,cur->aoi_object_id))
				leave_me(o,cur);
		}
				
		if(cur->in_myscope(cur,o)){
			 if(!is_set(cur->view_objs,o->aoi_object_id))
				enter_me(cur,o);
		}else{
			 if(is_set(cur->view_objs,o->aoi_object_id))
				leave_me(cur,o);
		}		
		cur = (struct aoi_object *)cur->block_node.next;
	}
}

static inline void block_process_leave(struct aoi_map *m,struct aoi_block *bl,struct aoi_object *o)
{
	if(dlist_empty(&bl->aoi_objs))return;
	struct aoi_object *cur = (struct aoi_object*)dlist_first(&bl->aoi_objs);
	while(cur != (struct aoi_object*)DLIST_TAIL(&bl->aoi_objs))
	{
		if(is_set(o->view_objs,cur->aoi_object_id) && !o->in_myscope(o,cur))leave_me(o,cur);
		if(is_set(cur->view_objs,o->aoi_object_id) && !cur->in_myscope(cur,o))leave_me(cur,o);
		cur = (struct aoi_object *)cur->block_node.next;
	}
}

int8_t move_to(struct aoi_map *m,struct aoi_object *o,int32_t _x,int32_t _y)
{
	struct point2D new_pos = {_x,_y};
	struct point2D old_pos = o->pos;
	if(0 != cal_blockset(m,&new_pos,&old_pos,o->view_radius)) return -1;
	
	o->pos = new_pos;
	struct aoi_block *old_block = get_block_by_point(m,&old_pos);
	struct aoi_block *new_block = get_block_by_point(m,&new_pos);
	if(old_block != new_block)
		dlist_remove(&o->block_node);

	uint32_t i;
	for(i = 0; m->enter_blocks[i];++i)
		block_process_enter(m,m->enter_blocks[i],o);
	
	for(i = 0; m->unchange_blocks[i];++i)
		block_process_unchange(m,m->leave_blocks[i],o);
	
	for(i = 0; m->leave_blocks[i];++i)
		block_process_leave(m,m->leave_blocks[i],o);
		
	if(old_block != new_block)
		dlist_push(&new_block->aoi_objs,&o->block_node);
	return 0;
}

int32_t enter_map(struct aoi_map *m,struct aoi_object *o,uint32_t radius,int32_t _x,int32_t _y)
{
	o->pos.x = _x;
	o->pos.y = _y;
	struct aoi_block *block = get_block_by_point(m,&o->pos);
	if(!block) return -1;
	o->aoi_object_id = get_id(m->_idmgr);
	if(o->aoi_object_id == -1) return -1;
	if(radius > m->radius) 
		radius = m->max_radius;
	else
		radius = m->radius;
	o->view_radius = radius;
	struct aoi_block **blocks = NEW_BLOCKS(m,&o->pos,radius);
	uint32_t i;
	for(i = 0; blocks[i];++i) block_process_enter(m,blocks[i],o);

	dlist_push(&block->aoi_objs,&o->block_node);
	enter_me(o,o);	
	return 0;
}

int32_t leave_map(struct aoi_map *m,struct aoi_object *o)
{
	struct aoi_block *block = get_block_by_point(m,&o->pos);
	if(!block) return -1;
	dlist_remove(&o->block_node);
	
	struct aoi_block **blocks = NEW_BLOCKS(m,&o->pos,o->view_radius);
	uint32_t i;
	for(i = 0; blocks[i];++i) block_process_leave(m,blocks[i],o);
	//自己离开自己的视野
	leave_me(o,o);
	return 0;			
}

void  destroy_map(struct aoi_map *m)
{
	free(m->new_blocks);
	free(m->old_blocks);
	free(m->enter_blocks);
	free(m->unchange_blocks);
	free(m->leave_blocks);
	del_bitset(m->new_block_set);
	del_bitset(m->old_block_set);
	destroy_idmgr(m->_idmgr);
	free(m);
}





/*
static inline void tick_super_object(struct map *m,struct aoi_object *o)
{
	uint32_t now = GetSystemMs();
	if(now - o->last_update_tick >= UPDATE_INTERVAL)
	{ 
		//remove out of view object first
		uint32_t i = 0;
		for( ; i < MAX_BITS; ++i)
		{
			if(o->self_view_objs.bits[i] > 0)
			{
				uint32_t j = 0;
				for( ; j < sizeof(uint32_t); ++j)
				{
					if(o->self_view_objs.bits[i] & (1 << j))
					{
						uint32_t aoi_object_id = i*sizeof(uint32_t) + j;
						if(aoi_object_id != o->aoi_object_id)
						{
							struct aoi_object *other = m->all_aoi_objects[aoi_object_id];
							if(other->is_leave_map)
								leave_me(m,o,other);
							else
							{
								uint64_t distance = cal_distance_2D(&o->current_pos,&other->current_pos);
								if(distance > o->view_radius)
									leave_me(m,o,other);
							}
						}
					}
				}
			}
		}
		//process enter view
		uint32_t x1,y1,x2,y2;
		x1 = y1 = x2 = y2 = 0;
		cal_blocks(m,&o->current_pos,o->view_radius,&x1,&y1,&x2,&y2);
		uint32_t y = y1;
		uint32_t x;
		for( ; y <= y2; ++y)
		{
			for( x=x1; x <= x2; ++x)
			{
				struct map_block *bl = get_block(m,y,x);
				struct aoi_object *cur = (struct aoi_object*)dlist_first(&bl->aoi_objs);
				while(cur != (struct aoi_object*)DLIST_TAIL(&bl->aoi_objs))
				{
					if(is_set(&o->self_view_objs,cur->aoi_object_id) == 0)
					{
						uint64_t distance = cal_distance_2D(&o->current_pos,&cur->current_pos);
						if(o->view_radius >= distance)
							enter_me(m,o,cur);
					}
					cur = (struct aoi_object *)cur->block_node.next;
				}
			}		
		}		
		o->last_update_tick = now;	
	}
}

void tick_super_objects(struct map *m)
{
	struct dnode *cur = m->super_aoi_objs.head.next;
	while(cur != &m->super_aoi_objs.tail)
	{
		struct aoi_object *o = (struct aoi_object*)((uint8_t*)cur - sizeof(struct dnode));
		tick_super_object(m,o);
		cur = cur->next;
	}
}
*/
