local class = require 'pl.class'
local UartNetx = class()


function UartNetx:_init(tLog)
  self.UART_CMD_Open = ${UART_CMD_Open}
  self.UART_CMD_RunSequence = ${UART_CMD_RunSequence}
  self.UART_CMD_Close = ${UART_CMD_Close}

  self.UART_SEQ_COMMAND_Clean = ${UART_SEQ_COMMAND_Clean}
  self.UART_SEQ_COMMAND_Send = ${UART_SEQ_COMMAND_Send}
  self.UART_SEQ_COMMAND_Receive = ${UART_SEQ_COMMAND_Receive}
  self.UART_SEQ_COMMAND_BaudRate = ${UART_SEQ_COMMAND_BaudRate}
  self.UART_SEQ_COMMAND_Delay = ${UART_SEQ_COMMAND_Delay}

  self.UART_HANDLE_SIZE = ${SIZEOF_UART_HANDLE_STRUCT}

  self.romloader = require 'romloader'
  self.lpeg = require 'lpeg'
  self.pl = require'pl.import_into'()

  self.tLog = tLog

  self.tGrammarMacro = self:__create_macro_grammar()

  self.ucDefaultRetries = 16
end



function UartNetx:__create_macro_grammar()
  local lpeg = self.lpeg

  local Space = lpeg.V('Space')
  local SinglequotedString = lpeg.V('SinglequotedString')
  local DoublequotedString = lpeg.V('DoublequotedString')
  local QuotedString = lpeg.V('QuotedString')
  local DecimalInteger = lpeg.V('DecimalInteger')
  local HexInteger = lpeg.V('HexInteger')
  local BinInteger = lpeg.V('BinInteger')
  local Integer = lpeg.V('Integer')
  local Data = lpeg.V('Data')
  local CleanCommand = lpeg.V('CleanCommand')
  local ReceiveCommand = lpeg.V('ReceiveCommand')
  local SendCommand = lpeg.V('SendCommand')
  local BaudRateCommand = lpeg.V('BaudRateCommand')
  local DelayCommand = lpeg.V('DelayCommand')
  local Command = lpeg.V('Command')
  local Comment = lpeg.V('Comment')
  local Statement = lpeg.V('Statement')
  local Program = lpeg.V('Program')

  return lpeg.P{ Program,
    -- An program is a list of statements separated by newlines.
    Program = Space * lpeg.Ct(lpeg.S("\r\n")^0 * Statement * (lpeg.S("\r\n")^1 * Statement)^0 * lpeg.S("\r\n")^0) * -1;

    -- A statement is either a command or a comment.
    Statement = Space * (Command + Comment) * Space;

    -- A comment starts with a hash and covers the complete line.
    Comment = lpeg.P('#') * (1 - lpeg.S("\r\n"))^0;

    -- A command is one of the 5 possible commands.
    Command = lpeg.Ct(Space * (CleanCommand + SendCommand + ReceiveCommand + BaudRateCommand + DelayCommand) * Comment^-1 * Space);

    -- A clean command has no parameter.
    CleanCommand = lpeg.Cg(lpeg.P("clean"), 'cmd');

    -- A send command has a data definition as parameters.
    SendCommand = lpeg.Cg(lpeg.P("send"), 'cmd') * Space * Data;

    -- A receive command has a length parameter, a total timeout and a char timeout.
    ReceiveCommand = lpeg.Cg(lpeg.P("receive"), 'cmd') * Space * lpeg.Cg(Integer, 'length') * Space * lpeg.P(',') * Space * lpeg.Cg(Integer, 'timeout_total') * Space * lpeg.P(',') * Space * lpeg.Cg(Integer, 'timeout_char');

    -- A baudrate command has the baud rate as the parameter.
    BaudRateCommand = lpeg.Cg(lpeg.P("baudrate"), 'cmd') * Space * lpeg.Cg(Integer, 'baudrate'); 

    -- A delay command has the delay in milliseconds as the parameter.
    DelayCommand = lpeg.Cg(lpeg.P("delay"), 'cmd') * Space * lpeg.Cg(Integer, 'delay'); 

    -- A data definition is a list of comma separated integers or strings surrounded by curly brackets. 
    Data = lpeg.Ct(lpeg.P('{') * Space * (lpeg.Cg(QuotedString) + lpeg.Cg(Integer)) * Space * (lpeg.P(',') * Space * (lpeg.Cg(QuotedString) + lpeg.Cg(Integer)))^0 * Space * lpeg.P('}'));

    -- A string can be either quoted or double quoted.
    SinglequotedString = lpeg.P("'") * ((1 - lpeg.S("'\r\n\f\\")) + (lpeg.P('\\') * 1))^0 * "'";
    DoublequotedString = lpeg.P('"') * ((1 - lpeg.S('"\r\n\f\\')) + (lpeg.P('\\') * 1))^0 * '"';
    QuotedString = lpeg.P(SinglequotedString + DoublequotedString);

    -- An integer can be decimal, hexadecimal or binary.
    DecimalInteger = lpeg.R('09')^1;
    HexInteger = lpeg.P("0x") * lpeg.P(lpeg.R('09') + lpeg.R('af'))^1;
    BinInteger = lpeg.P("0b") * lpeg.R('01')^1;
    Integer = lpeg.P(HexInteger + BinInteger + DecimalInteger);

    -- Whitespace.
    Space = lpeg.S(" \t")^0;
  }
end



function UartNetx:initialize(tPlugin)
  local tLog = self.tLog
  local romloader = self.romloader
  local tester = _G.tester

  local astrBinaryName = {
    [romloader.ROMLOADER_CHIPTYP_NETX4000_RELAXED] = '4000',
    [romloader.ROMLOADER_CHIPTYP_NETX4000_FULL]    = '4000',
    [romloader.ROMLOADER_CHIPTYP_NETX4100_SMALL]   = '4000',
--    [romloader.ROMLOADER_CHIPTYP_NETX500]          = '500',
--    [romloader.ROMLOADER_CHIPTYP_NETX100]          = '500',
--    [romloader.ROMLOADER_CHIPTYP_NETX90_MPW]       = '90_mpw',
    [romloader.ROMLOADER_CHIPTYP_NETX90]           = '90',
--    [romloader.ROMLOADER_CHIPTYP_NETX56]           = '56',
--    [romloader.ROMLOADER_CHIPTYP_NETX56B]          = '56',
--    [romloader.ROMLOADER_CHIPTYP_NETX50]           = '50',
--    [romloader.ROMLOADER_CHIPTYP_NETX10]           = '10'
  }

  -- Get the binary for the ASIC.
  local tAsicTyp = tPlugin:GetChiptyp()
  local strBinary = astrBinaryName[tAsicTyp]
  if strBinary==nil then
    local strMsg = string.format('No binary for chip type %s.', tAsicTyp)
    tLog.error(strMsg)
    error(strMsg)
  end
  local strNetxBinary = string.format('netx/uart_netx%s.bin', strBinary)
  tLog.debug('Loading binary "%s"...', strNetxBinary)

  local aAttr = tester:mbin_open(strNetxBinary, tPlugin)
  tester:mbin_debug(aAttr)
  tester:mbin_write(tPlugin, aAttr)

  return {
    plugin = tPlugin,
    attr = aAttr
  }
end



function UartNetx:__parseNumber(strNumber)
  local tResult
  if string.sub(strNumber, 1, 2)=='0b' then
    tResult = 0
    local uiPos = 0
    for iPos=string.len(strNumber),3,-1 do
      local uiDigit = string.byte(strNumber, iPos) - 0x30
      tResult = tResult + uiDigit * math.pow(2, uiPos)
      uiPos = uiPos + 1
    end
  else
    tResult = tonumber(strNumber)
  end

  return tResult
end



function UartNetx:__uint16_to_bytes(usData)
  local ucB1 = math.floor(usData/256)
  local ucB0 = usData - 256*ucB1

  return ucB0, ucB1
end



function UartNetx:__uint32_to_bytes(ulData)
  local ucB3 = math.floor(ulData/0x01000000)
  ulData = ulData - 0x01000000*ucB3
  local ucB2 = math.floor(ulData/0x00010000)
  ulData = ulData - 0x00010000*ucB2
  local ucB1 = math.floor(ulData/0x00000100)
  local ucB0 = ulData - 0x00000100*ucB1

  return ucB0, ucB1, ucB2, ucB3
end



function UartNetx:parseMacro(strMacro)
  local lpeg = self.lpeg
  local tLog = self.tLog
  local pl = self.pl

  local uiExpectedReadData = 0
  local tResult = lpeg.match(self.tGrammarMacro, strMacro)
  if tResult==nil then
    error('Failed to parse the macro...')
  else
--    pl.pretty.dump(tResult)

    -- Collect the merged commands here.
    local astrMacro = {}

    for uiCommandCnt, tRawCommand in ipairs(tResult) do
      local strCmd = tRawCommand.cmd
      if strCmd=='clean' then
        table.insert(astrMacro, string.char(
          self.UART_SEQ_COMMAND_Clean
        ))

      elseif strCmd=='receive' then
        -- Create a new receive command.
        local ucLen0, ucLen1 = self:__uint16_to_bytes(self:__parseNumber(tRawCommand.length))
        local ucTT0, ucTT1 = self:__uint16_to_bytes(self:__parseNumber(tRawCommand.timeout_total))
        local ucTC0, ucTC1 = self:__uint16_to_bytes(self:__parseNumber(tRawCommand.timeout_char))
        table.insert(astrMacro, string.char(
          self.UART_SEQ_COMMAND_Receive,
          ucLen0, ucLen1,
          ucTT0, ucTT1,
          ucTC0, ucTC1
        ))
        uiExpectedReadData = uiExpectedReadData + tRawCommand.length

      elseif strCmd=='send' then
        -- Create a new send command.
        local tCmd = {
          cmd = 'send',
          data = nil
        }
        -- Collect the data.
        local astrData = {}
        local astrReplace = {
          ['\\"'] = '"',
          ["\\'"] = "'",
          ['\\a'] = '\a',
          ['\\b'] = '\b',
          ['\\f'] = '\f',
          ['\\n'] = '\n',
          ['\\r'] = '\r',
          ['\\t'] = '\t',
          ['\\v'] = '\v'
        }
        for uiDataElement, strData in ipairs(tRawCommand[1]) do
          if string.sub(strData, 1, 1)=='"' or string.sub(strData, 1, 1)=="'" then
            -- Unquote the string.
            strData = string.sub(strData, 2, -2)
            -- Unescape the string.
            strData = string.gsub(strData, '(\\["\'abfnrtv])', astrReplace)
            table.insert(astrData, strData)
          else
            local uiData = self:__parseNumber(strData)
            if uiData<0 or uiData>255 then
              tLog.error('Data element %d of command %d exceeds the 8 bit range: %d.', uiData, uiCommandCnt, tData)
              error('Invalid data.')
            end
            table.insert(astrData, string.char(uiData))
          end
        end
        local strData = table.concat(astrData)
        local ucLen0, ucLen1 = self:__uint16_to_bytes(string.len(strData))

        table.insert(astrMacro, string.char(
          self.UART_SEQ_COMMAND_Send,
          ucLen0, ucLen1
        ))
        table.insert(astrMacro, strData)

      elseif strCmd=='baudrate' then
        -- Create a new baudrate command.
        local ucB0, ucB1, ucB2, ucB3 = self:__uint32_to_bytes(tCmd.baudrate)
        table.insert(astrMacro, string.char(
          self.UART_SEQ_COMMAND_BaudRate,
          ucB0, ucB1, ucB2, ucB3
        ))

      elseif strCmd=='delay' then
        -- Create a new delay command.
        local ucD0, ucD1, ucD2, ucD3 = self:__uint32_to_bytes(tCmd.delay)
        table.insert(astrMacro, string.char(
          self.UART_SEQ_COMMAND_Delay,
          ucD0, ucD1, ucD2, ucD3
        ))

      end
    end

    tResult = table.concat(astrMacro)
  end

  return tResult, uiExpectedReadData
end



function UartNetx:openDevice(tHandle, uiUart, ulBaudRate, atMMIO, atPortcontrol)
  ulBaudRate = ulBaudRate or 115200
  atMMIO = atMMIO or {}
  atPortcontrol = atPortcontrol or {}
  ucMMIO_RX = atMMIO.RX or 0xff
  ucMMIO_TX = atMMIO.TX or 0xff
  ucMMIO_RTS = atMMIO.RTS or 0xff
  ucMMIO_CTS = atMMIO.CTS or 0xff
  usPortcontrol_RX = atPortcontrol.RX or 0xffff
  usPortcontrol_TX = atPortcontrol.TX or 0xffff
  usPortcontrol_RTS = atPortcontrol.RTS or 0xffff
  usPortcontrol_CTS = atPortcontrol.CTS or 0xffff

  local tLog = self.tLog
  local tester = _G.tester
  local aAttr = tHandle.attr

  -- Setup a basic layout of the buffer:
  --   * Parameter (fixed size: 64 bytes)
  --   * Handle (fixed size: UART_HANDLE_SIZE bytes)
  --   * RX/TX buffer
  tHandle.ulHandleAddress = aAttr.ulParameterStartAddress + 64
  tHandle.ulBufferAddress = aAttr.ulParameterStartAddress + 64 + self.UART_HANDLE_SIZE

  -- Combine all options.
  local ucC0, ucC1, ucC2, ucC3 = self:__uint32_to_bytes(uiUart)
  local ucB0, ucB1, ucB2, ucB3 = self:__uint32_to_bytes(ulBaudRate)
  local ucPRX0, ucPRX1 = self:__uint16_to_bytes(usPortcontrol_RX)
  local ucPTX0, ucPTX1 = self:__uint16_to_bytes(usPortcontrol_TX)
  local ucPRTS0, ucPRTS1 = self:__uint16_to_bytes(usPortcontrol_RTS)
  local ucPCTS0, ucPCTS1 = self:__uint16_to_bytes(usPortcontrol_CTS)
  local strOptions = string.char(
    ucC0, ucC1, ucC2, ucC3,
    ucB0, ucB1, ucB2, ucB3,
    ucMMIO_RX,
    ucMMIO_TX,
    ucMMIO_RTS,
    ucMMIO_CTS,
    ucPRX0, ucPRX1,
    ucPTX0, ucPTX1,
    ucPRTS0, ucPRTS1,
    ucPCTS0, ucPCTS1
  )

  local tPlugin = tHandle.plugin
  if tPlugin==nil then
    tLog.error('The handle has no "plugin" set.')
  else
    -- Run the command.
    local aParameter = {
      0xffffffff,    -- verbose
      self.UART_CMD_Open,
      tHandle.ulHandleAddress
    }
    tester:mbin_set_parameter(tPlugin, aAttr, aParameter)
    -- Append the options.
    tester:stdWrite(tPlugin, aAttr.ulParameterStartAddress+0x18, strOptions)

    ulValue = tester:mbin_execute(tPlugin, aAttr, aParameter)
    if ulValue~=0 then
      tLog.error('Failed to open the device.')
      error('Failed to open the device.')
    end
  end
end



function UartNetx:run_sequence(tHandle, strSequence, sizExpectedRxData)
  local tLog = self.tLog
  local tester = _G.tester
  local tResult

  local aAttr = tHandle.attr

  local sizTxBuffer = string.len(strSequence)
  local pucTxBuffer = tHandle.ulBufferAddress
  local pucRxBuffer = tHandle.ulBufferAddress + sizTxBuffer

  local tPlugin = tHandle.plugin
  if tPlugin==nil then
    tLog.error('The handle has no "plugin" set.')
  else
    -- Download the sequence data.
    tester:stdWrite(tPlugin, pucTxBuffer, strSequence)

    -- Run the command.
    local aParameter = {
      0xffffffff,    -- verbose
      self.UART_CMD_RunSequence,
      tHandle.ulHandleAddress,
      pucTxBuffer,
      sizTxBuffer,
      pucRxBuffer,
      sizExpectedRxData,
      'OUTPUT'
    }
    tester:mbin_set_parameter(tPlugin, aAttr, aParameter)
    ulValue = tester:mbin_execute(tPlugin, aAttr, aParameter)
    if ulValue~=0 then
      tLog.error('Failed to run the sequence.')
    else
      -- Get the size of the result data from the output parameter.
      local sizResultData = aParameter[7]
      tLog.debug('The netX reports %d bytes of result data.', sizResultData)

      -- Read the result data.
      local strResultData = tester:stdRead(tPlugin, pucRxBuffer, sizResultData)
      tResult = strResultData
    end
  end

  return tResult
end



function UartNetx:closeDevice(tHandle)
  local tLog = self.tLog
  local tester = _G.tester
  local aAttr = tHandle.attr

  local tPlugin = tHandle.plugin
  if tPlugin==nil then
    tLog.error('The handle has no "plugin" set.')
  else
    -- Run the command.
    local aParameter = {
      0xffffffff,    -- verbose
      self.UART_CMD_Close,
      tHandle.ulHandleAddress
    }
    tester:mbin_set_parameter(tPlugin, aAttr, aParameter)

    ulValue = tester:mbin_execute(tPlugin, aAttr, aParameter)
    if ulValue~=0 then
      tLog.error('Failed to close the device.')
      error('Failed to close the device.')
    end
  end
end


return UartNetx
