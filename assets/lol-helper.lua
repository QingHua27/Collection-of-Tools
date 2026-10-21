EnablePrimaryModify = false;  --是否允许修饰符开启宏
EnablePrimaryPoll = false; --是否允许修改轮询
spring={}
spring.init = {}
spring.nullFunc = function(...)return ... end;  --空函数
nullFunc = spring.nullFunc;
spring.modify_key = {{'lshift'},{'lctrl'},{'lalt'},{'rshift'},{'rctrl'},{'ralt'}}
init = spring.nullFunc--init初始化

----------------------------主程序----------------------------
function OnEvent(event,arg,family)
	if event == "PROFILE_ACTIVATED" then
        	--  初始化 
        ClearLog();--清空控制台
	   	list=List:new();--数组初始化
	  	spring.spin(spring.gc,10*60*1000)('start');--10min调用一次gc回收资源
		main = main or spring.controller();
		for i = 1,#spring.init do
			spring.init[i]();
		end
	   	init();--用户配置初始化
	elseif family == "mouse" and (event == "M_RELEASED"or event=="M_PRESSED") then
	else
		local str = arg;
		for i,key in ipairs(spring.modify_key) do 
			if IsModifierPressed(key[1])then str = key[1].."+"..str end
		end
		--从按键配置表里读取
		local fun_arr = main(nil,str,event) or main(nil,arg,event);	
		if fun_arr then 
			if fun_arr[1]==spring.run then 
				spring.run(unpack(fun_arr[2]))
			else
				spring.run(fun_arr[1],unpack(fun_arr[2]))
			end
		end
	end 
	if family == "mouse" and event == "M_RELEASED" then
		if(EnablePrimaryModify)then
			for i,key in ipairs(spring.modify_key) do
				local v_status =  IsModifierPressed(key[1])
				if(not key[2] == v_status)then 
					key[2]=v_status
					local fun_arr = main(nil,key[1],v_status and 1 or 0);
					if fun_arr then 
						if fun_arr[1]==spring.run then 
							spring.run(unpack(fun_arr[2]))
						else
							spring.run(fun_arr[1],unpack(fun_arr[2]))
						end
					end
				end
			end
		end
		if(EnablePrimaryPoll and poll)then poll() end;
		spring.Poll()
		
	end
end
-- 轮询初始化
do
	spring.M_State = GetMKeyState("mouse");
	SetMKeyState(spring.M_State, "mouse");
	spring.poll_step = 1 ;--轮询时间片
end

-- 轮询
function spring.Poll()
	local next_thread = list:get();
	while next_thread do
		local i,f,err=pcall(coroutine.resume, next_thread)
		if not f then log(err);all_stop(next_thread)end 
		next_thread = list:get();
	end
	spring.Sleep(spring.poll_step);
	SetMKeyState(spring.M_State, "mouse");

end



--sleep方法 
function sleep(time)
	if type(time)~="number"then error('error: sleep param error',2);coroutine.yield();return end;
	if time<=0 then return end
	local now_thread = coroutine.running();
	local next_time = GetRunningTime()+time;
	list:add(next_time,now_thread);
	coroutine.yield();
end

spring.Sleep = Sleep
Sleep = function(arg)
	if(not coroutine.running())then 
		error('error: cannot run sleep in mainThread!',2)
		return
	end
	return sleep(arg)
end

-- 主控制器
function spring.controller()
	local array = {}
	return function(func,g_num,event,...)
		if event==nil or event==1 then event = 'MOUSE_BUTTON_PRESSED' end;
		if event==0 then event = 'MOUSE_BUTTON_RELEASED' end;
		if not func then 
			return (array[g_num]==nil and {nil}or{array[g_num][event]})[1];
		end
		g_num = g_num==2 and 3 or g_num==3 and 2 or g_num;
		if array[g_num]==nil then array[g_num]={}end;
		array[g_num][event]={func,{ ... }};
	end
end

--重写log调试
function log(...)
	local str ="";
	for i = 1,#arg do str=str..tostring(arg[i]);end
	OutputLogMessage(" %s \n",tostring(str))
end
spring.log=log

--run函数的模板函数
function spring.run_func(func,...)
	local arr={...}; 
	return function()func(unpack(arr))end 
end

--添加线程，并立即调用
--传入参数为方法
function run(func,...)
	if type(func)~="function"then  error('error: run param error',2);return nil end
	local cor = coroutine.create(spring.run_func(func,...));
	list:add(-1,cor);
	return cor;
end
spring.run = run


spring.pressed_key={}
--按键
function press(_arg)
	if type(_arg)=="number" then PressMouseButton(_arg)elseif type(_arg)=="string" then PressKey(_arg)end
	if not spring.pressed_key[_arg] then spring.pressed_key[_arg]=coroutine.running() end
end 
function release(_arg)
	if type(_arg)=="number" then ReleaseMouseButton(_arg)elseif type(_arg)=="string" then ReleaseKey(_arg)end
	if spring.pressed_key[_arg] then spring.pressed_key[_arg]=nil end
end
function pressAndRelease(_arg)
	if type(_arg)=="number" then PressAndReleaseMouseButton (_arg)elseif type(_arg)=="string" then PressAndReleaseKey(_arg)end
end
function moveTo(x,y)
	if(y)then 
		MoveMouseTo(x,y)
	else
		local x_y = string.split(x,",");
		MoveMouseTo(x_y[1],x_y[2]) 
	end
end
function getPos()
	local x,y = GetMousePosition();
	spring.log('Position:(',x,',',y,')\t\trunTime:',GetRunningTime());
	return x,y
end
function all_stop(v_cor)
	if v_cor then list:delete(v_cor) else list:empty()end
	for key,cor in pairs(spring.pressed_key) do
		if not v_cor or cor==v_cor then release(key) end
	end
end

spring.all_stop=all_stop
--垃圾回收
function spring.gc()
	-- spring.log('before gc:',collectgarbage("count"),'kb');
	collectgarbage("collect");
	-- spring.log('after gc:',collectgarbage("count"),'kb');
end
--自旋方法
--[[
@author	残叶00 	2019/9/1
@parms	func 为循环调用的方法（禁止在func里使用sleep函数）	--@function func(parm) ...;  return parm; end
		time 为循环时间
		init 为循环参数初始值 可省
		end_func 循环终f止后调用的方法 可省
		... 循环终止后调用的方法参数 可省

@return 	spin 对象
		@parms	--	'start'	--开启自旋		
			--	'end'	--关闭自旋并调用end_func		--@return end_func的返回值
			--	'status'--当前自旋开启状态			--@return 运行中true
			--	'get'	--当前自旋状态参数			--@return 循环方法的参数
]]
function spin(func,time,init,end_func,...)
	local flag = false;
	local cor ;
	local function new_fun(func,init,time,end_func,...) 
		local i = init ;
		local args={ ... }
		return function(arg) 
			if arg == 1 then return i;
			elseif arg == 0 then return (end_func and{end_func(unpack(args))}or{nil})[1] end;
			while true do
				i=func(i);
				local t = (type(time)=="function"and{time()}or{time})[1]
				sleep(t);
			end;
		end;
	end;
	local _fun = new_fun(func,init,time,end_func,...)
	return function(arg) 
		if arg==nil then arg = 'start_end'end
		if arg=='start_end' then arg=(flag and'end'or'start')end -- 开关
		if arg=='start' and flag~=true then
			cor=spring.run(_fun);
			flag = true
		elseif arg=='end' then 
			list:delete(cor);   
			flag = false;
			return _fun(0);
		elseif arg=="get" then 
			return _fun(1);
		elseif arg=="status" then
			return flag;
		end;
	end
end
spring.spin = spin
--自旋方法 end ---


--序列控制代码
function queue_ctrl(v_queue)
	local flag = false -- 记录宏是否打开
	local v_thread = nil -- 记录宏的协程对象
	local pressed_key = {};
	return function(arg)
		
		if arg==nil then arg = 'start_end'end
		if arg=='start_end' then arg=(flag and'end'or'start')end -- 开关
		if v_thread and not list:has(v_thread) and arg=='end' then arg='start'end
		if arg=='start' then -- 开启
			flag=true
			v_thread = spring.run(v_queue) -- 执行序列
		elseif arg=='end'then -- 关闭
			flag=false
			spring.all_stop(v_thread)
		elseif arg=='status' then return flag; -- 返回运行状态 
		
		end
	end
end

--多线程连点器
--[[
@description 将序列数组整理成多个自旋对象，并进行统一调度
@author	残叶00 	2019/9/2
@parms	序列数组
	{
		{按键,		按键间隔,	按下与抬起时间间隔（默认50ms）,		拦截器函数},
		{"q",		20000		},							--每20s点一次q
	    	{3,		1000,		100,  					click_arr_w},	--每秒点击一次鼠标右键并按照click_arr_w函数拦截

		{开始执行函数，		结束执行函数},
		{start_fun,		end_fun	}
	}

	拦截器模板
	function click_arr_w(get)				
		if GetRunningTime()-get(1)>=15000 then 
			log('wait'); 
			return true;
		else return false;
	end end

@return 	连点器对象
		@parms	--	'start'	--开启连点		
			--	'end'	--关闭连点			--@return end_func的返回值
			--	'status'--当前序列开启状态		--@return 运行中true
			--	数字	--根据下标获取自旋数组对应对象	--@return 自旋对象
]]
function clicks(array)
	local flag=false
	local spin_arr={}
	local arr=array;
	local function main_fun(arg)
		if arg==nil then arg = 'start_end'end
		if arg=='start_end' then arg=(flag and'end'or'start')end -- 开关
		if arg=='start' and flag==false then -- 开启
			for i=1,#array do
				if(type(array[i])=="function")then spring.run(array[i])
				elseif (type(array[i])=="table"and type(array[i][1])=="function")then 
					spin_arr[i]=array[i][3]
					spring.run(array[i][1])
				elseif (type(array[i])=="table")then
					local key = array[i][1];--按键
					local between_p_r = array[i][3];--按下抬起间隔
					if between_p_r==nil then between_p_r=0 end;--默认0ms间隔
					local sleep_time = array[i][2]-between_p_r;--间隔
					local case_when = array[i][4];--阻隔器
					if(not case_when)then case_when=function()end;end;
					local function click(s)
						local l_flag,l_time = case_when(main_fun);
						while l_flag do
							sleep(l_time or sleep_time+between_p_r)
							l_flag,l_time = case_when(main_fun);
						end
						press(key);
						sleep(between_p_r);
						release(key);
						return GetRunningTime();
					end
					local obj_spin = spring.spin(click,sleep_time,-999999,release,key);
					spin_arr[i]=obj_spin
					obj_spin("start");
				end
			end
			flag=true;
		elseif arg=='end'and flag then -- 关闭
			for i=1,#array do
				if(type(array[i])=="table" and type(array[i][2])=="function")then spring.run(array[i][2])
				elseif (type(array[i])=="table")then
					spin_arr[i]("end");
				end
			end
			flag=false
			spin_arr={};
		elseif arg=='status' then return flag; -- 返回运行状态 

		elseif type(arg)=='number' then 
			return spin_arr[arg]and spin_arr[arg]('get'); -- 返回时间
		end
	end
	return main_fun;
end
--多线程连点器 end ---
--序列运行器
function Queue (array,start_func,end_func,end_queue_func)
	local runningTime = -999999;
	local prev_step ; --上一步
	local next_step ; --下一步
	local flag=false; 
	local main_fun;
	local function do_queue(v_step)--主序列
		prev_step = v_step;
		if array[v_step]==nil then --序列运行到底
			if end_queue_func==nil then next_step=1; -- 若序列结束函数为空，则重置序列
			else end_queue_func(v_step); end;	--	否则执行序列结束函数;
		else	--未运行到底，则正常运行
			next_step = v_step + 1
			local case_when = array[v_step][4];--阻隔器
			if(case_when==nil)then case_when=function()end;end;
			local l_flag = case_when(main_fun);
			if l_flag then
				return v_step;
			end
			array[v_step][1](array[v_step][2]);
			runningTime = GetRunningTime();
		end	
		return next_step; --运行下一步
	end
	local function sleep_func()--sleep函数
		if array[prev_step]==nil or array[prev_step][3]==nil then return 0;
		else return array[prev_step][3]end;
	end
	function end_func_proxy()
		for i=1,#array do
			if(array[i] and array[i][1]==press)then
				release(array[i][2])
			end
		end;
		if(end_func)then end_func()end
	end;
	local que_spin = spring.spin(do_queue,sleep_func,1,end_func_proxy);--生成自旋对象
	main_fun = function(arg)
		if arg==nil then arg = 'start_end'end
		if arg=='start_end' then arg=(flag and'end'or'start')end -- 开关
		if arg=='start' and flag==false then
			flag = true
			spring.run(function()
				if start_func then start_func()end;--执行初始函数
				que_spin("start");	--执行序列
			end);
		elseif arg=='end' and flag==true then
			flag = false
			spring.run(function()que_spin("end");end);--关闭序列
		elseif arg=='status' then return flag;
		elseif arg=='get' then return runningTime,que_spin("get");
		elseif type(arg)=='number'then next_step=arg;--修改步数
		end
	end
	return main_fun;
end



-- 插排队列 --------------------------
List = {root = nil,size = 0};
----- 
function List:new(o,root,size)--新建对象
	o =  o or {}
	setmetatable(o,self)
	self.__index=self
	o.root = root
	o.size = size
	return o;
end
function List:add(key,value)--存入键值对
	local node = Node:new(key,value)
	self:insert(node);
end
function List:get()--通过当前时间获取值
	if self.root==nil then return nil end
	local node = self.root;
	if node.key-GetRunningTime()<spring.poll_step then 
		self.root=node.next_node;
		self.size = self.size-1;
		return node.value;
	end
	return nil;
end

function List:empty()self.root = nil;end--清空队列
function List:length()return self.size;end--获得集合长度
---------private--
Node = {key = nil,value=nil,next_node=nil};
function Node:new (key,value,next_node,o)
	o = o or {}
	setmetatable(o,self)
	self.__index=self
	o.key = key
	o.value  = value
	o.next_node = next_node
	return o;
end
function List:insert(node)--插入调整
	self.size=self.size+1
	local tmp_node;
	if self.root==nil then 
		self.root=node ;
		return;
	elseif self.root.key >=node.key then
		node.next_node=self.root
		self.root=node
		return;
	else 
		tmp_node=self.root;
	end
	while tmp_node.next_node~=nil do
		if tmp_node.next_node.key>=node.key then
			node.next_node=tmp_node.next_node;
			tmp_node.next_node=node;
			return;
		else
			tmp_node=tmp_node.next_node;
		end
	end 
	tmp_node.next_node=node
end
function List:delete(value)--删除调整
	if self.root==nil then 
		return false;
	elseif self.root.value==value then
		self.root=self.root.next_node;
		return true;
	end
	local tmp_node = self.root;
	while tmp_node.next_node~=nil do
		if tmp_node.next_node.value==value then
			tmp_node.next_node=tmp_node.next_node.next_node;
			return true;
		end
		tmp_node=tmp_node.next_node;
	end
	return false;
end
function List:has(value)--存在
	if self.root==nil then 
		return false;
	elseif self.root.value==value then
		return true;
	end
	local tmp_node = self.root;
	while tmp_node.next_node~=nil do
		if tmp_node.next_node.value==value then
			return true;
		end
		tmp_node=tmp_node.next_node;
	end
	return false;
end
-- 插排队列 end--------------------------

--[[
function spring.shallow_copy(orig)
  local copy
  if type(orig) == "table" then
    copy = {}
    for orig_key, orig_value in pairs(orig) do
      copy[orig_key] = orig_value
    end
  else -- number, string, boolean, etc
    copy = orig
  end
  return copy
end


spring._G = spring.shallow_copy(_G)
]]
--拆分字符串
string.split = function(s, p)
    local rt= {}
    string.gsub(s, '[^'..p..']+', function(w) table.insert(rt, w) end )
    return rt
end
--生成只读表
function hid_tab (t,setter)
	local tmp = {}
	local mt = {
		__index = t,
		__newindex = setter or function(t,k,v)
			error("cannot update a read-only table !",2)
		end
	}
	setmetatable(tmp,mt)
	return tmp
end



spring=hid_tab(spring)



-- 拦截器模板
function comb_key(...)		
	local keys = {  ...  };
	return function()		 
		for i = 1,#keys do --删除已被按下的按键
			local k = keys[i];
			if spring.pressed_key[_arg] then table.remove(keys,i)end;
			press(k);
		end
		run(function()
			sleep(50);
			for i = 1,#keys do 
				release(keys[i]);
			end
		end) 
	end
end


Script_Switch = "capslock" 
ZA = 4   -- 4 键：开启走A开关
GB = 5   -- 5 键：关闭走A开关
PING = 55 -- 网络延迟补偿(ms)

EnablePrimaryMouseButtonEvents(1)

local cached_windup = 100
local cached_move = 300
local cached_AS = 1.0

-- 1. 安全读取 D:\LOL.txt 并计算精准前摇与移动延迟
function updateSpeedConfig()
    local success, config = pcall(dofile, "D:\\LOL.txt")
    if success and type(config) == "table" and config.attackSpeed then
        local attackSpeed = tonumber(config.attackSpeed) or 1.0
        local totalInterval = math.floor(1000 / attackSpeed)
        
        local windup = math.floor(totalInterval * 0.31) + math.floor(PING / 2) + 15
        local move = totalInterval - windup
        
        if move < 10 then move = 10 end

        cached_windup = windup
        cached_move = move
        cached_AS = attackSpeed
    end
    return cached_windup, cached_move, cached_AS
end

-- 2. 后台高频预加载
function backgroundFetch()
    while true do
        if IsKeyLockOn(Script_Switch) and not IsMouseButtonPressed(1) then
            updateSpeedConfig()
        end
        Sleep(50)
    end
end

-- 3. 极速响应 + 零延迟走A核心（含大写开关判定）
function zoua()
    -- 【关键限制】：若大写灯未亮起，直接退出，不按 C 键也不走 A
    if not IsKeyLockOn(Script_Switch) then return end

    -- 仅在大写亮起时，按下左键秒按 C 键（显示射程）
    PressKey("c")

    local next_action_time = GetRunningTime()

    while IsKeyLockOn(Script_Switch) and IsMouseButtonPressed(1) do
        local windupTime = cached_windup
        local moveTime = cached_move
        local currentAS = cached_AS
        local totalInterval = windupTime + moveTime

        -- 仅在非第一发平A时校验 CD，确保起手瞬间零延迟
        if GetRunningTime() < next_action_time then
            while GetRunningTime() < next_action_time and IsMouseButtonPressed(1) do
                Sleep(1)
            end
        end

        if not IsMouseButtonPressed(1) or not IsKeyLockOn(Script_Switch) then break end

        -- ① 发起平A指令 (A + 点击)
        PressKey("a")
        Sleep(10)
        ReleaseKey("a")

        local attack_start_time = GetRunningTime()

        -- 【精准前摇控制】等待平A抬手与伤害判定完成
        local windup_end_time = attack_start_time + windupTime
        while GetRunningTime() < windup_end_time do
            Sleep(1)
        end

        -- 【攻速突变防御】：前摇结束瞬间校验最新攻速
        local new_windup, new_move, new_AS = updateSpeedConfig()
        if new_AS < currentAS then
            local extra_windup = new_windup - windupTime
            if extra_windup > 0 then
                local extra_end_time = GetRunningTime() + extra_windup
                while GetRunningTime() < extra_end_time do
                    Sleep(1)
                end
            end
            windupTime = new_windup
            moveTime = new_move
            totalInterval = windupTime + moveTime
        end

        -- ② 发起右键移动指令
        PressMouseButton(3)
        Sleep(10)
        ReleaseMouseButton(3)

        -- 刷新下一次平A许可时间
        next_action_time = attack_start_time + totalInterval

        OutputLogMessage("AS: %.2f | Windup: %dms | Move: %dms\n", currentAS, windupTime, moveTime)
    end

    -- 松开左键或关闭大写时瞬间释放 C 键
    ReleaseKey("c")
end

-- 4. 按键控制逻辑
function kg()
    if not IsKeyLockOn(Script_Switch) then
        PressAndReleaseKey(Script_Switch)
    end
    if IsMouseButtonPressed(1) then
        zoua()
    end
end

function gb()
    if IsKeyLockOn(Script_Switch) then
        PressAndReleaseKey(Script_Switch)
    end
    StopAll()
end

function StopAll()
    if list and list.empty then list:empty() end
    ReleaseMouseButton(1)
    ReleaseMouseButton(3)
    ReleaseKey("c")
end

-- 5. 框架初始化注册
function init()
    main(run, ZA, "MOUSE_BUTTON_PRESSED", kg)   -- 鼠标 4 键开启开关
    main(run, GB, "MOUSE_BUTTON_PRESSED", gb)   -- 鼠标 5 键关闭开关
    main(run, 1,  "MOUSE_BUTTON_PRESSED", zoua) -- 按住鼠标左键走A

    run(backgroundFetch)
end